#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int zip_extract(const char *archive, const char *member,
                const char *outfile);
int convert_to_qh(void);
int pack_database(const char *output_filename,
                  const char *qh_filename);

typedef struct {
    int archive;
    const char *member;
    const char *filename;
} EXTRACT;

static const EXTRACT files[] = {
    {0,"INTERRUP.A","interrup.a"},
    {0,"INTERRUP.B","interrup.b"},
    {0,"INTERRUP.C","interrup.c"},
    {0,"INTERRUP.D","interrup.d"},
    {1,"INTERRUP.E","interrup.e"},
    {1,"INTERRUP.F","interrup.f"},
    {1,"INTERRUP.G","interrup.g"},
    {1,"INTERRUP.H","interrup.h"},
    {1,"INTERRUP.I","interrup.i"},
    {2,"INTERRUP.J","interrup.j"},
    {2,"INTERRUP.K","interrup.k"},
    {2,"INTERRUP.L","interrup.l"},
    {2,"INTERRUP.M","interrup.m"},
    {2,"INTERRUP.N","interrup.n"},
    {3,"INTERRUP.O","interrup.o"},
    {3,"INTERRUP.P","interrup.p"},
    {3,"INTERRUP.Q","interrup.q"},
    {3,"INTERRUP.R","interrup.r"},
    {1,"GLOSSARY.LST","glossary.lst"},
    {2,"INTERRUP.1ST","interrup.1st"},
    {2,"MEMORY.LST","memory.lst"},
    {2,"INTERRUP.PRI","interrup.pri"},
    {3,"86BUGS.LST","86bugs.lst"},
    {3,"CMOS.LST","cmos.lst"},
    {3,"PORTS.A","ports.a"},
    {3,"PORTS.B","ports.b"},
    {3,"PORTS.C","ports.c"}
};

static int combine_sections(const char *prefix, int count,
                            const char *output_name)
{
    FILE *out;
    FILE *in;
    char input_name[32];
    char buffer[8192];
    size_t n;
    int i;

    out = fopen(output_name, "wb");
    if (!out) {
        fprintf(stderr, "Cannot create %s\n", output_name);
        return 1;
    }

    for (i = 0; i < count; ++i) {
        printf("\rCombining %-8s section %d/%d",
               prefix, i + 1, count);
        fflush(stdout);

        sprintf(input_name, "%s.%c", prefix, 'a' + i);

        in = fopen(input_name, "rb");
        if (!in) {
            fprintf(stderr, "Cannot open %s\n", input_name);
            fclose(out);
            return 1;
        }

        while ((n = fread(buffer, 1, sizeof(buffer), in)) != 0) {
            if (fwrite(buffer, 1, n, out) != n) {
                fprintf(stderr, "Write error on %s\n", output_name);
                fclose(in);
                fclose(out);
                return 1;
            }
        }

        if (ferror(in)) {
            fprintf(stderr, "Read error on %s\n", input_name);
            fclose(in);
            fclose(out);
            return 1;
        }

        fclose(in);

        if (fwrite("\r\n", 1, 2, out) != 2) {
            fprintf(stderr, "Write error on %s\n", output_name);
            fclose(out);
            return 1;
        }
    }

    fclose(out);
    printf("\rCombined %d sections into %s%20s\n",
           count, output_name, " ");
    fflush(stderr);
    fflush(stdout);
    

    return 0;
}


static int report_database_crc(const char *filename)
{
    FILE *fp;
    unsigned long crc = 0xFFFFFFFFUL;
    unsigned long size = 0;
    int ch;
    int bit;

    fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "Cannot read output database: %s\n", filename);
        return 1;
    }

    while ((ch = fgetc(fp)) != EOF) {
        crc ^= (unsigned long)(unsigned char)ch;
        for (bit = 0; bit < 8; ++bit) {
            if (crc & 1UL)
                crc = (crc >> 1) ^ 0xEDB88320UL;
            else
                crc >>= 1;
        }
        ++size;
    }

    if (ferror(fp)) {
        fprintf(stderr, "Read error on output database: %s\n", filename);
        fclose(fp);
        return 1;
    }

    fclose(fp);
    crc ^= 0xFFFFFFFFUL;
    printf("Database size: %lu bytes\n", size);
    printf("Database CRC32: %08lX\n", crc);
    return 0;
}

int main(int argc, char **argv)
{
    char archives[6][128];
    char basepath[128];
    FILE *fp;
    size_t i;
    size_t total = sizeof(files) / sizeof(files[0]);
    size_t intlistexists = 1;

    printf("RBILBLD by Gregory Fenton, github.com/gregoryfenton\n"
           "Convert Ralf Brown's Intlist v6.1 to a searchable "
           "database suitable for RBILVIEW\n"
    );

    if (argc < 2 || argc > 3) {
        fprintf(stderr,
            "Usage: RBILBLD [output RBD file] {optional path to INTER61?.ZIP)\n"
            "  examples:\n"
            "    RBILBLD RBIL.RBD C:\\INTLIST\\\n"
            "    RBILBLD RBIL.RBD\n"
            " no path will search the current directory\n"
        );
        return 1;
    }

    //safely build basepath with a trailing slash
    if(argc == 3) {
            size_t len;
            strncpy(basepath, argv[2], sizeof(basepath) - 1);
            basepath[sizeof(basepath) - 1] = '\0';
                
            len = strlen(basepath);
            if(len > 0 && basepath[len - 1] != '/') {
                if(len < sizeof(basepath) - 2) {
                        strcat(basepath, "/");
                }
            } else {
                // .\ or ./ depending on OS
                basepath[0] = '.';
                basepath[1] = '/';
                basepath[2] = 0;
            }
    }    
    
    for (i = 0; i < 6; ++i) {
        char suffix[16];
        sprintf(suffix, "INTER61%c.ZIP", (int)('A' + i));
        sprintf(archives[i], "%s%s", 
                  basepath, suffix);

        fp = fopen(archives[i], "rb");
        if (!fp) {
            fprintf(stderr, "Cannot open archive: %s\n", archives[i]);
            fflush(stderr);
            intlistexists = 0;
        }
        else {
          fclose(fp);
        }
    }

    if(!intlistexists) {
        fprintf(stderr, "You require Ralf Brown's original intlist zip files.\n"
                "Search online, you need all six:\n"
        );
        for (i = 0; i < 6; ++i) {
          printf("%s ", archives[i]);
        }
        printf("\n");
        
        return 1;
    }

    for (i = 0; i < total; ++i) {
        printf("\rExtracting %u/%u: %-12s",
               (unsigned)(i + 1),
               (unsigned)total,
               files[i].member);
        fflush(stdout);

        if (zip_extract(archives[files[i].archive],
                        files[i].member, files[i].filename) != 0) {
            printf("\rExtraction FAILED: %-12s%20s\n",
                   files[i].member," ");
            fflush(stdout);
            return 1;
        }
    }

    printf("\rExtraction successful: %u files extracted.%20s\n",
           (unsigned)total, " ");
    fflush(stdout);

    if (combine_sections("interrup", 18, "interrup.lst") != 0)
        return 1;

    if (combine_sections("ports", 3, "ports.lst") != 0)
        return 1;

    if (convert_to_qh() != 0) {
        fprintf(stderr, "Converter failed\n");
        return 1;
    }

    if (pack_database(argv[1], "genrated.qh") != 0) {
        fprintf(stderr, "Packer failed\n");
        return 1;
    }
    printf("Checking database CRC\n");
    if (report_database_crc(argv[1]) != 0) {
        fprintf(stderr, "Could not verify output database CRC32\n");
        return 1;
    }

    printf("Cleaning up\n");
    for (i = 0; i < total; ++i) {
        remove(files[i].filename);
    }
    remove("interrup.lst");
    remove("ports.lst");
    remove("genrated.qh");

    printf("RBIL database build complete\n\n");
    
    printf("The laborious bit is over, now go and play with\n"
           "Ralf Brown's wonderful intlist.\n"
           "RBILVIEW %s\n", argv[1]
    );
    return 0;
}
