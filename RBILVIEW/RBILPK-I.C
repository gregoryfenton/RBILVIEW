
/*
 * RBILPACK.C
 *
 * RBIL QuickHelp source packer.
 * Target: OpenWatcom C 1.9, 32-bit DOS / DOS32A.
 *
 * Usage:
 *   RBILPACK RBIL.RBD HEADER.QH MEMLIST.QH PORTS.QH ...
 *
 * Database format:
 *   Header followed by fixed-size index records, then strings and bodies.
 *   All offsets and lengths are 32-bit.
 *
 * This program does not modify the source files.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef unsigned int U32;

#define MAX_TOPICS 20000
#define LINE_SIZE  8192

typedef struct {
    char *id;
    char *title;
    char *category;
    char *source;
    char *body;
    U32 body_len;
} TOPIC;

typedef struct {
    char magic[8];
    U32 version;
    U32 topic_count;
    U32 index_offset;
    U32 text_offset;
    U32 file_size;
} HEADER;

typedef struct {
    U32 id_off;
    U32 title_off;
    U32 category_off;
    U32 source_off;
    U32 body_off;
    U32 body_len;
} INDEXREC;

static TOPIC topics[MAX_TOPICS];
static U32 topic_count = 0;

static void fatal(const char *message)
{
    fprintf(stderr, "RBILPACK: %s\n", message);
    exit(1);
}

static char *duplicate(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);

    if (!p)
        fatal("out of memory");

    memcpy(p, s, n);
    return p;
}

static void replace_string(char **dst, const char *src)
{
    char *p = duplicate(src);
    free(*dst);
    *dst = p;
}

static int starts_ci(const char *s, const char *prefix)
{
    while (*prefix) {
        if (!*s ||
            tolower((unsigned char)*s) !=
            tolower((unsigned char)*prefix))
            return 0;

        ++s;
        ++prefix;
    }

    return 1;
}

static void trim(char *s)
{
    size_t n = strlen(s);
    char *p;

    while (n &&
           (s[n-1] == '\r' || s[n-1] == '\n' ||
            s[n-1] == ' '  || s[n-1] == '\t'))
        s[--n] = '\0';

    p = s;
    while (*p == ' ' || *p == '\t')
        ++p;

    if (p != s)
        memmove(s, p, strlen(p) + 1);
}

static void append_body(TOPIC *t, const char *s)
{
    size_t n = strlen(s);
    char *p;

    if ((U32)n > 0xFFFFFFFFUL - t->body_len - 1)
        fatal("topic body too large");

    p = (char *)realloc(
        t->body,
        (size_t)t->body_len + n + 1
    );

    if (!p)
        fatal("out of memory growing topic");

    t->body = p;

    memcpy(t->body + t->body_len, s, n);
    t->body_len += (U32)n;
    t->body[t->body_len] = '\0';
}

static TOPIC *new_topic(const char *id, const char *source)
{
    TOPIC *t;

    if (topic_count >= MAX_TOPICS)
        fatal("maximum topic count exceeded");

    t = &topics[topic_count++];
    memset(t, 0, sizeof(*t));

    t->id = duplicate(id);
    t->title = duplicate(id);
    t->category = duplicate("");
    t->source = duplicate(source);
    t->body = duplicate("");

    return t;
}

static const char *basename_of(const char *path)
{
    const char *p = strrchr(path, '\\');
    const char *q = strrchr(path, '/');

    if (q && (!p || q > p))
        p = q;

    return p ? p + 1 : path;
}

static void parse_file(const char *filename)
{
    FILE *fp;
    char line[LINE_SIZE];
    TOPIC *current = NULL;
    char *context_id = NULL;
    char *context_category = NULL;
    int saw_topic = 0;
    const char *source = basename_of(filename);
    U32 topics_before = topic_count;
    long file_size;
    long last_reported = -1;

    fp = fopen(filename, "rb");

    if (!fp) {
        fprintf(stderr, "RBILPACK: cannot open %s\n", filename);
        exit(1);
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        fatal("cannot determine input file size");
    }

    file_size = ftell(fp);
    if (file_size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        fatal("cannot determine input file size");
    }

    while (fgets(line, sizeof(line), fp)) {
        char work[LINE_SIZE];
        char *value;
        size_t len = strlen(line);

        long position = ftell(fp);

        if (file_size > 0 && position >= 0) {
            long percent = (position * 100L) / file_size;

            if (percent != last_reported) {
                printf("\rParsing %-12s: %3ld%%  (%u topics)",
                       source, percent, topic_count - topics_before);
                fflush(stdout);
                last_reported = percent;
            }
        }

        /* Normalize line endings to LF. */
        if (len && line[len-1] == '\n') {
            line[--len] = '\0';
            if (len && line[len-1] == '\r')
                line[--len] = '\0';
        } else if (len && line[len-1] == '\r') {
            line[--len] = '\0';
        }

        strncpy(work, line, sizeof(work)-1);
        work[sizeof(work)-1] = '\0';
        trim(work);

        if (starts_ci(work, ".context")) {
            value = work + 8;
            while (*value == ' ' || *value == '\t')
                ++value;

            /*
             * A consecutive .context can replace a provisional
             * context before it has acquired a topic or body.
             * Discard that unused record rather than indexing
             * an empty entry.
             */
            if (current && !saw_topic &&
                current == &topics[topic_count - 1] &&
                current->body_len == 0 &&
                current->category[0] == '\0' &&
                strcmp(current->id, current->title) == 0) {
                free(current->id);
                free(current->title);
                free(current->category);
                free(current->source);
                free(current->body);
                memset(current, 0, sizeof(*current));
                --topic_count;
                current = NULL;
            }

            free(context_id);
            free(context_category);

            context_id = duplicate(value);
            context_category = duplicate("");
            current = new_topic(value, source);
            saw_topic = 0;
            continue;
        }

        if (current && starts_ci(work, ".category")) {
            value = work + 9;
            while (*value == ' ' || *value == '\t')
                ++value;

            replace_string(&context_category, value);
            replace_string(&current->category, value);
            continue;
        }

        if (current && starts_ci(work, ".topic")) {
            value = work + 6;
            while (*value == ' ' || *value == '\t')
                ++value;

            if (saw_topic) {
                current = new_topic(context_id, source);
                replace_string(&current->category,
                               context_category);
            }

            replace_string(&current->title, value);
            saw_topic = 1;
            continue;
        }

        if (!current)
            continue;

        /* QuickHelp control directives are not body text. */
        if (work[0] == '.')
            continue;

        append_body(current, line);
        append_body(current, "\n");
    }

    fclose(fp);
    free(context_id);
    free(context_category);

    printf("\rParsed %-12s: 100%%  (%u topics added)          \n",
           source, topic_count - topics_before);
    fflush(stdout);

}

static U32 file_position(FILE *fp)
{
    long pos = ftell(fp);

    if (pos < 0)
        fatal("ftell failed");

    return (U32)pos;
}

static U32 write_string(FILE *fp, const char *s)
{
    U32 offset = file_position(fp);
    size_t n = strlen(s) + 1;

    if (fwrite(s, 1, n, fp) != n)
        fatal("failed writing string");

    return offset;
}

static void write_database(const char *filename)
{
    FILE *fp;
    HEADER h;
    INDEXREC *index;
    U32 i;

    fp = fopen(filename, "wb+");

    if (!fp) {
        fprintf(stderr, "RBILPACK: cannot create %s\n", filename);
        exit(1);
    }

    memset(&h, 0, sizeof(h));
    memcpy(h.magic, "RBILDB1", 7);

    h.version = 1;
    h.topic_count = topic_count;
    h.index_offset = sizeof(HEADER);
    h.text_offset =
        h.index_offset + topic_count * sizeof(INDEXREC);

    index = (INDEXREC *)calloc(
        topic_count ? (size_t)topic_count : 1,
        sizeof(INDEXREC)
    );

    if (!index)
        fatal("out of memory allocating index");

    /* Reserve header and index space. */
    if (fwrite(&h, sizeof(h), 1, fp) != 1)
        fatal("header write failed");

    for (i = 0; i < topic_count; ++i) {
        if (fwrite(&index[i], sizeof(INDEXREC), 1, fp) != 1)
            fatal("index reservation failed");
    }

    /* Store the common source label once, shared by every topic. */
    {
        U32 shared_source_off =
            write_string(fp, "genrated.qh") - h.text_offset;

        for (i = 0; i < topic_count; ++i) {
            index[i].id_off =
                write_string(fp, topics[i].id) - h.text_offset;

            index[i].title_off =
                write_string(fp, topics[i].title) - h.text_offset;

            index[i].category_off =
                write_string(fp, topics[i].category) - h.text_offset;

            index[i].source_off = shared_source_off;

            index[i].body_off = file_position(fp);
        index[i].body_len = topics[i].body_len;

        if (topics[i].body_len &&
            fwrite(topics[i].body, 1,
                   topics[i].body_len, fp) !=
                   topics[i].body_len)
                fatal("topic body write failed");
        }
    }

    h.file_size = file_position(fp);

    /* Write the finished index. */
    if (fseek(fp, (long)h.index_offset, SEEK_SET) != 0)
        fatal("cannot seek to index");

    if (fwrite(index, sizeof(INDEXREC),
               topic_count, fp) != topic_count)
        fatal("index write failed");

    /* Write the finished header. */
    if (fseek(fp, 0L, SEEK_SET) != 0)
        fatal("cannot seek to header");

    if (fwrite(&h, sizeof(h), 1, fp) != 1)
        fatal("header update failed");

    fclose(fp);
    free(index);
}

static void release_topics(void)
{
    U32 i;

    for (i = 0; i < topic_count; ++i) {
        free(topics[i].id);
        free(topics[i].title);
        free(topics[i].category);
        free(topics[i].source);
        free(topics[i].body);
    }
}

int pack_database(const char *output_filename,
                  const char *qh_filename)
{
    printf("Reading QuickHelp source: %s\n", qh_filename);
    parse_file(qh_filename);

    printf("Writing %u topics to %s\n",
           topic_count, output_filename);
    write_database(output_filename);
    release_topics();
    topic_count = 0;

    printf("Finished.\n");
    return 0;
}
