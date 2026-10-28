#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "TINFL.C"

static uint16_t u16(const unsigned char *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t crc32_bytes(const unsigned char *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFUL;
    size_t i;
    int b;
    for (i = 0; i < n; ++i) {
        c ^= p[i];
        for (b = 0; b < 8; ++b)
            c = (c >> 1) ^ ((c & 1) ? 0xEDB88320UL : 0);
    }
    return c ^ 0xFFFFFFFFUL;
}

static unsigned char *read_file(const char *name, size_t *len)
{
    FILE *f = fopen(name, "rb");
    long n;
    unsigned char *p;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return NULL;
    }
    p = (unsigned char *)malloc((size_t)n ? (size_t)n : 1);
    if (!p || fread(p, 1, (size_t)n, f) != (size_t)n) {
        free(p);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return p;
}

int zip_extract(const char *archive, const char *member,
                const char *outfile)
{
    unsigned char *z, *compressed, *output;
    size_t zlen, i;
    uint32_t crc, csize, usize, local;
    uint16_t method, flags, fnlen, extralen, cfnlen, cexlen, ccomlen;
    int found = 0;
    FILE *f;
    tinfl_decompressor d;
    tinfl_status status;
    size_t in_n, out_n;

    z = read_file(archive, &zlen);
    if (!z) {
        fprintf(stderr, "Cannot read archive: %s\n", archive);
        return 1;
    }

    /* Search backwards for the End of Central Directory record. */
    if (zlen < 22) {
        fprintf(stderr, "Archive too short\n");
        free(z);
        return 1;
    }

    i = zlen - 22;
    for (;;) {
        if (u32(z + i) == 0x06054B50UL)
            break;
        if (i == 0 || zlen - i > 22 + 65535) {
            fprintf(stderr, "ZIP end record not found\n");
            free(z);
            return 1;
        }
        --i;
    }

    {
        uint16_t disk = u16(z + i + 4);
        uint16_t cd_disk = u16(z + i + 6);
        uint16_t entries_disk = u16(z + i + 8);
        uint16_t entries = u16(z + i + 10);
        uint32_t cd_size = u32(z + i + 12);
        uint32_t cd_off = u32(z + i + 16);
        size_t p = cd_off;
        unsigned int k;

        if (disk || cd_disk || entries_disk != entries ||
            (uint64_t)cd_off + cd_size > zlen) {
            fprintf(stderr, "Unsupported ZIP layout\n");
            free(z);
            return 1;
        }

        for (k = 0; k < entries; ++k) {
            if (p + 46 > zlen || u32(z + p) != 0x02014B50UL) {
                fprintf(stderr, "Bad central-directory entry\n");
                free(z);
                return 1;
            }

            flags = u16(z + p + 8);
            method = u16(z + p + 10);
            crc = u32(z + p + 16);
            csize = u32(z + p + 20);
            usize = u32(z + p + 24);
            cfnlen = u16(z + p + 28);
            cexlen = u16(z + p + 30);
            ccomlen = u16(z + p + 32);
            local = u32(z + p + 42);

            if (p + 46UL + cfnlen + cexlen + ccomlen > zlen) {
                fprintf(stderr, "Central-directory entry out of range\n");
                free(z);
                return 1;
            }

            if (strlen(member) == cfnlen &&
                memcmp(z + p + 46, member, cfnlen) == 0) {
                found = 1;
                break;
            }
            p += 46UL + cfnlen + cexlen + ccomlen;
        }
    }

    if (!found) {
        fprintf(stderr, "Member not found: %s\n", member);
        free(z);
        return 1;
    }

    if ((flags & 1) || (method != 8 && method != 0)) {
        fprintf(stderr, "Encrypted or unsupported member (flags=%u method=%u)\n",
                flags, method);
        free(z);
        return 1;
    }

    if ((size_t)local + 30 > zlen ||
        u32(z + local) != 0x04034B50UL) {
        fprintf(stderr, "Bad local file header\n");
        free(z);
        return 1;
    }

    fnlen = u16(z + local + 26);
    extralen = u16(z + local + 28);
    local += 30UL + fnlen + extralen;

    if ((uint64_t)local + csize > zlen) {
        fprintf(stderr, "Compressed member out of range\n");
        free(z);
        return 1;
    }

    compressed = z + local;
    output = (unsigned char *)malloc(usize ? (size_t)usize : 1);
    if (!output) {
        fprintf(stderr, "Out of memory for output\n");
        free(z);
        return 1;
    }

    if (method == 0) {
        if (csize != usize) {
            fprintf(stderr, "Invalid stored-member sizes\n");
            free(output);
            free(z);
            return 1;
        }
        memcpy(output, compressed, usize);
    } else {
        tinfl_init(&d);
        in_n = csize;
        out_n = usize;
        status = tinfl_decompress(&d, compressed, &in_n,
                                  output, output, &out_n,
                                  TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        if (status != TINFL_STATUS_DONE || in_n != csize || out_n != usize) {
            fprintf(stderr, "Deflate failed: status=%d input=%lu output=%lu\n",
                    (int)status, (unsigned long)in_n, (unsigned long)out_n);
            free(output);
            free(z);
            return 1;
        }
    }

    if (crc32_bytes(output, usize) != crc) {
        fprintf(stderr, "CRC32 mismatch\n");
        free(output);
        free(z);
        return 1;
    }

    f = fopen(outfile, "wb");
    if (!f || fwrite(output, 1, usize, f) != usize) {
        fprintf(stderr, "Cannot write output: %s\n", outfile);
        if (f) fclose(f);
        free(output);
        free(z);
        return 1;
    }
    fclose(f);

    free(output);
    free(z);
    return 0;
}
