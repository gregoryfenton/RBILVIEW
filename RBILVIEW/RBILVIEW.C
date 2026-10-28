/*
 * RBILVIEW.C
 *
 * RBIL QuickHelp database viewer.
 *
 * Target:
 *   Open Watcom C/C++
 *   DOS/32 Advanced / Linux
 *
 * Build:
 *   wcl386 -bt=dos -l=dos32a -fe=RBILVIEW.EXE RBILVIEW.C
 *   wcl386 -bt=linux -l=linux -fe=RBILVIEW.LX RBILVIEW.C
 *
 * Usage:
 *   RBILVIEW RBIL.RBD
 *
 * Commands:
 *   Number    Open a topic by its displayed number
 *   /text     Search topic titles, contexts and categories
 *   N         Next index page
 *   P         Previous index page
 *   Q         Quit
 *
 * This version writes directly to VGA text video RAM (0xB8000) for flicker-free performance on DOS,
 * and uses ncurses safely on Linux.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#define INDEX_FOOTER "N/P Next/previous match or page  Esc Back  Q Quit  F1 Help"
#define TOPIC_FOOTER "N/P Next/previous topic  Esc Index"

#define APP_TITLE  "RBIL QuickHelp"
#define APP_AUTHOR "Gregory Fenton"
#define APP_CREDIT APP_TITLE " - " APP_AUTHOR

#ifdef __linux__
#include <stdint.h>
#include <ncurses.h>
typedef uint32_t U32;
#else
#include <conio.h>
#include <i86.h>
typedef unsigned long U32;
#endif

#define MAGIC          "RBILDB1"
#define DB_VERSION     1UL
#define PAGE_SIZE      18
#define INPUT_SIZE     256
#define READ_SIZE      4096

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

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

typedef struct {
    INDEXREC rec;
    char *id;
    char *title;
    char *category;
    char *source;
} TOPIC;

static FILE *db_file = NULL;
static HEADER db_header;
static TOPIC *topics = NULL;
static U32 topic_count = 0;
static char *string_table = NULL;
static U32 string_table_size = 0;

/*
 * Colour editor blocks.
 */

#ifndef __linux__

static volatile struct {
    unsigned char marker_start[19];
    unsigned char COL_SCREEN_FG;
    unsigned char COL_SCREEN_BG;
    unsigned char COL_MENU_FG;
    unsigned char COL_MENU_BG;
    unsigned char COL_SELECTED_FG;
    unsigned char COL_SELECTED_BG;
    unsigned char COL_HEADER_FG;
    unsigned char COL_HEADER_BG;
    unsigned char COL_STATUS_FG;
    unsigned char COL_STATUS_BG;
    unsigned char marker_end[23];
} colours_dos = {
    "COLOURS_HEADER_DOS",
    7, 1,       /* Screen: light grey on blue */
    7, 1,       /* Menu: light grey on blue */
    3, 1,       /* Selected: cyan on blue */
    14, 1,      /* Header: bright yellow on blue */
    11, 1,      /* Status: bright cyan on blue */
    "COLOURS_HEADER_DOS_END"
};

#else

static volatile struct {
    unsigned char marker_start[21];
    unsigned char COL_SCREEN_FG;
    unsigned char COL_SCREEN_BG;
    unsigned char COL_MENU_FG;
    unsigned char COL_MENU_BG;
    unsigned char COL_SELECTED_FG;
    unsigned char COL_SELECTED_BG;
    unsigned char COL_HEADER_FG;
    unsigned char COL_HEADER_BG;
    unsigned char COL_STATUS_FG;
    unsigned char COL_STATUS_BG;
    unsigned char BOLD_SCREEN;
    unsigned char BOLD_MENU;
    unsigned char BOLD_SELECTED;
    unsigned char BOLD_HEADER;
    unsigned char BOLD_STATUS;
    unsigned char marker_end[25];
} colours_linux = {
    "COLOURS_HEADER_LINUX",
    7, 4,       /* Screen: white on blue */
    7, 4,       /* Menu: white on blue */
    6, 4,       /* Selected: cyan on blue */
    3, 4,       /* Header: yellow on blue */
    6, 4,       /* Status: cyan on blue */
    0, 0, 0, 0, 0, /* Per-role bold flags, initially off */
    "COLOURS_HEADER_LINUX_END"
};

#endif

/* ------------------------------------------------------------ */

#ifdef __linux__

static int ui_screen_height = 25;
static int ui_screen_width = 80;
static int ui_index_rows = 18;
static int ui_body_rows = 19;

static int ui_colour_value(unsigned char fg)
{
    if (fg >= 8 && COLORS < 16)
        return fg & 7;
    return fg;
}

static int ui_role_foreground(unsigned char attr)
{
    switch (attr) {
    case 0x19: return colours_linux.COL_SCREEN_FG;
    case 0x71: return colours_linux.COL_SELECTED_FG;
    case 0x1E: return colours_linux.COL_HEADER_FG;
    case 0x1B: return colours_linux.COL_STATUS_FG;
    default:   return colours_linux.COL_MENU_FG;
    }
}

static int ui_role_bold(unsigned char attr)
{
    switch (attr) {
    case 0x19: return colours_linux.BOLD_SCREEN;
    case 0x71: return colours_linux.BOLD_SELECTED;
    case 0x1E: return colours_linux.BOLD_HEADER;
    case 0x1B: return colours_linux.BOLD_STATUS;
    default:   return colours_linux.BOLD_MENU;
    }
}

static short ui_colour_pair(unsigned char attr)
{
    switch (attr) {
    case 0x19: return 1;
    case 0x71: return 3;
    case 0x1E: return 4;
    case 0x1B: return 5;
    default:   return 2;
    }
}

static void ui_set_cursor(int row, int col)
{
    if (row < 0) row = 0;
    if (col < 0) col = 0;
    if (row >= ui_screen_height) row = ui_screen_height - 1;
    if (col >= ui_screen_width) col = ui_screen_width - 1;
    if (row < 0 || col < 0)
        return;

    move(row, col);
    refresh();
}

static void write_vga_string(int row, int col, const char *str,
                             unsigned char attr, int max_len)
{
    int i;
    int width;
    int ended = (str == NULL);

    if (row < 0 || row >= ui_screen_height ||
        col < 0 || col >= ui_screen_width || max_len <= 0)
        return;

    width = max_len;
    if (width > ui_screen_width - col)
        width = ui_screen_width - col;

    attron(COLOR_PAIR(ui_colour_pair(attr)));
    if (((attr & 0x0F) & 8) ||
        ui_role_bold(attr) ||
        (ui_role_foreground(attr) >= 8 && COLORS < 16))
        attron(A_BOLD);

    move(row, col);
    for (i = 0; i < width; ++i) {
        unsigned char ch = ' ';

        if (!ended) {
            if (str[i] == '\0') {
                ended = 1;
            } else {
                ch = (unsigned char)str[i];
            }
        }

        addch(ch);
    }

    if (((attr & 0x0F) & 8) ||
        ui_role_bold(attr) ||
        (ui_role_foreground(attr) >= 8 && COLORS < 16))
        attroff(A_BOLD);
    attroff(COLOR_PAIR(ui_colour_pair(attr)));
    move(0, 0);
    refresh();
}

static void clear_screen(void)
{
    erase();
    move(0, 0);
    refresh();
}

static void ui_update_layout(void)
{
    int rows, cols;

    getmaxyx(stdscr, rows, cols);

    if (rows < 1)
        rows = 25;
    if (cols < 1)
        cols = 80;

    ui_screen_height = rows;
    ui_screen_width = cols;
    ui_index_rows = rows - 6;
    ui_body_rows = rows - 6;

    if (ui_index_rows < 1)
        ui_index_rows = 1;
    if (ui_body_rows < 1)
        ui_body_rows = 1;
}

static int ui_init(void)
{
    if (initscr() == NULL)
        return 0;

    ui_update_layout();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    if (has_colors()) {
        start_color();

        init_pair(1, ui_colour_value(colours_linux.COL_SCREEN_FG),
                     colours_linux.COL_SCREEN_BG);
        init_pair(2, ui_colour_value(colours_linux.COL_MENU_FG),
                     colours_linux.COL_MENU_BG);
        init_pair(3, ui_colour_value(colours_linux.COL_SELECTED_FG),
                     colours_linux.COL_SELECTED_BG);
        init_pair(4, ui_colour_value(colours_linux.COL_HEADER_FG),
                     colours_linux.COL_HEADER_BG);
        init_pair(5, ui_colour_value(colours_linux.COL_STATUS_FG),
                     colours_linux.COL_STATUS_BG);

        bkgdset(COLOR_PAIR(1));
    }

    return 1;
}

static void ui_shutdown(void)
{
    endwin();
}

#else

static int ui_screen_height = 25;
static int ui_screen_width = 80;
static int ui_index_rows = 18;
static int ui_body_rows = 19;

static unsigned char ui_dos_attr(unsigned char attr)
{
    int fg, bg;

    switch (attr) {
    case 0x19:
        fg = colours_dos.COL_SCREEN_FG;
        bg = colours_dos.COL_SCREEN_BG;
        break;
    case 0x1F:
        fg = colours_dos.COL_MENU_FG;
        bg = colours_dos.COL_MENU_BG;
        break;
    case 0x71:
        fg = colours_dos.COL_SELECTED_FG;
        bg = colours_dos.COL_SELECTED_BG;
        break;
    case 0x1E:
        fg = colours_dos.COL_HEADER_FG;
        bg = colours_dos.COL_HEADER_BG;
        break;
    case 0x1B:
        fg = colours_dos.COL_STATUS_FG;
        bg = colours_dos.COL_STATUS_BG;
        break;
    default:
        return attr;
    }

    return (unsigned char)(((bg & 7) << 4) | (fg & 15));
}

static void ui_set_cursor(int row, int col)
{
    union REGS inregs, outregs;

    if (row < 0) row = 0;
    if (col < 0) col = 0;
    if (row >= ui_screen_height) row = ui_screen_height - 1;
    if (col >= ui_screen_width) col = ui_screen_width - 1;
    if (row < 0 || col < 0)
        return;

    memset(&inregs, 0, sizeof(inregs));
    memset(&outregs, 0, sizeof(outregs));
    inregs.h.ah = 0x02;
    inregs.h.bh = 0;
    inregs.h.dh = (unsigned char)row;
    inregs.h.dl = (unsigned char)col;
    int386(0x10, &inregs, &outregs);
}

static void write_vga_string(int row, int col, const char *str,
                             unsigned char attr, int max_len)
{
    unsigned short *vga = (unsigned short *)0xB8000;
    int i = 0;
    int width;

    attr = ui_dos_attr(attr);

    if (row < 0 || row >= ui_screen_height ||
        col < 0 || col >= ui_screen_width || max_len <= 0)
        return;

    width = max_len;
    if (width > ui_screen_width - col)
        width = ui_screen_width - col;

    while (i < width && str[i] != '\0') {
        vga[row * ui_screen_width + col + i] =
            (unsigned short)((attr << 8) | (unsigned char)str[i]);
        i++;
    }

    while (i < width) {
        vga[row * ui_screen_width + col + i] =
            (unsigned short)((attr << 8) | ' ');
        i++;
    }

    ui_set_cursor(0, 0);
}

static void clear_screen(void)
{
    unsigned short *vga = (unsigned short *)0xB8000;
    unsigned short blank =
        (unsigned short)((ui_dos_attr(0x19) << 8) | ' ');
    int i;
    int cells = ui_screen_width * ui_screen_height;

    for (i = 0; i < cells; ++i)
        vga[i] = blank;

    ui_set_cursor(0, 0);
}

static int ui_init(void)
{
    union REGS inregs, outregs;
    int columns = 80;
    int rows = 25;

    memset(&inregs, 0, sizeof(inregs));
    memset(&outregs, 0, sizeof(outregs));
    inregs.h.ah = 0x0F;
    int386(0x10, &inregs, &outregs);

    if (outregs.h.ah >= 20)
        columns = outregs.h.ah;

    memset(&inregs, 0, sizeof(inregs));
    memset(&outregs, 0, sizeof(outregs));
    inregs.h.ah = 0x11;
    inregs.h.al = 0x30;
    inregs.h.bh = 0;
    int386(0x10, &inregs, &outregs);

    if (outregs.h.dl >= 9)
        rows = (int)outregs.h.dl + 1;

    if (columns < 20 || columns > 255)
        columns = 80;
    if (rows < 10 || rows > 100)
        rows = 25;

    ui_screen_width = columns;
    ui_screen_height = rows;

    ui_index_rows = rows - 6;
    ui_body_rows = rows - 6;

    if (ui_index_rows < 1)
        ui_index_rows = 1;
    if (ui_body_rows < 1)
        ui_body_rows = 1;

    return 1;
}

static void ui_shutdown(void)
{
    clear_screen();
    ui_set_cursor(0, 0);
}

#endif

static char *get_string(U32 offset)
{
    if (string_table == NULL)
        return "";

    if (offset >= string_table_size)
        return "";

    return string_table + offset;
}

static int load_database(const char *filename)
{
    U32 i;
    U32 index_bytes;
    U32 strings_offset;
    INDEXREC *raw_index = NULL;

    db_file = fopen(filename, "rb");

    if (db_file == NULL) {
        printf("Cannot open database: %s\n", filename);
        return 0;
    }

    if (fread(&db_header, sizeof(db_header), 1, db_file) != 1) {
        printf("Cannot read database header.\n");
        fclose(db_file);
        db_file = NULL;
        return 0;
    }

    if (memcmp(db_header.magic, MAGIC, strlen(MAGIC)) != 0) {
        printf("Not an RBIL database file.\n");
        fclose(db_file);
        db_file = NULL;
        return 0;
    }

    if (db_header.version != DB_VERSION) {
        printf("Unsupported database version: %lu\n",
               (unsigned long)db_header.version);
        fclose(db_file);
        db_file = NULL;
        return 0;
    }

    topic_count = db_header.topic_count;

    if (topic_count == 0) {
        printf("The database contains no topics.\n");
        fclose(db_file);
        db_file = NULL;
        return 0;
    }

    if (topic_count > 1000000UL) {
        printf("Database topic count is unreasonable.\n");
        fclose(db_file);
        db_file = NULL;
        return 0;
    }

    if (topic_count > (U32)(SIZE_MAX / sizeof(TOPIC))) {
        printf("Database is too large for this viewer.\n");
        fclose(db_file);
        db_file = NULL;
        return 0;
    }

    topics = (TOPIC *)calloc((size_t)topic_count, sizeof(TOPIC));

    if (topics == NULL) {
        printf("Not enough memory for topic index.\n");
        fclose(db_file);
        db_file = NULL;
        return 0;
    }

    if (topic_count > (U32)(SIZE_MAX / sizeof(INDEXREC))) {
        printf("Database index is too large.\n");
        fclose(db_file);
        db_file = NULL;
        return 0;
    }

    index_bytes = topic_count * (U32)sizeof(INDEXREC);

    raw_index = (INDEXREC *)malloc((size_t)index_bytes);

    if (raw_index == NULL) {
        printf("Not enough memory for database index.\n");
        fclose(db_file);
        db_file = NULL;
        return 0;
    }

    if (fseek(db_file, (long)db_header.index_offset, SEEK_SET) != 0) {
        printf("Cannot seek to database index.\n");
        free(raw_index);
        fclose(db_file);
        db_file = NULL;
        return 0;
    }

    if (fread(raw_index, sizeof(INDEXREC), (size_t)topic_count,
              db_file) != (size_t)topic_count) {
        printf("Cannot read database index.\n");
        free(raw_index);
        fclose(db_file);
        db_file = NULL;
        return 0;
    }

    strings_offset = db_header.text_offset;

    if (strings_offset > db_header.file_size) {
        printf("Invalid database string-table offset.\n");
        free(raw_index);
        fclose(db_file);
        db_file = NULL;
        return 0;
    }

    string_table_size = db_header.file_size - strings_offset;

    if (string_table_size == 0) {
        printf("Database string table is empty.\n");
        free(raw_index);
        fclose(db_file);
        db_file = NULL;
        return 0;
    }

    string_table = (char *)malloc((size_t)string_table_size);

    if (string_table == NULL) {
        printf("Not enough memory for database strings.\n");
        free(raw_index);
        fclose(db_file);
        db_file = NULL;
        return 0;
    }

    if (fseek(db_file, (long)strings_offset, SEEK_SET) != 0) {
        printf("Cannot seek to database strings.\n");
        free(raw_index);
        fclose(db_file);
        db_file = NULL;
        return 0;
    }

    if (fread(string_table, 1, (size_t)string_table_size, db_file)
        != (size_t)string_table_size) {
        printf("Cannot read database strings.\n");
        free(raw_index);
        fclose(db_file);
        db_file = NULL;
        return 0;
    }

    for (i = 0; i < topic_count; ++i) {
        topics[i].rec = raw_index[i];

        topics[i].id = get_string(raw_index[i].id_off);
        topics[i].title = get_string(raw_index[i].title_off);
        topics[i].category = get_string(raw_index[i].category_off);
        topics[i].source = get_string(raw_index[i].source_off);
    }

    free(raw_index);

    return 1;
}

/* ------------------------------------------------------------ */

static void close_database(void)
{
    if (db_file != NULL) {
        fclose(db_file);
        db_file = NULL;
    }

    if (topics != NULL) {
        free(topics);
        topics = NULL;
    }

    if (string_table != NULL) {
        free(string_table);
        string_table = NULL;
    }
}

/* ------------------------------------------------------------ */

static int contains_nocase(const char *haystack, const char *needle)
{
    size_t needle_len;
    const char *p;
    size_t i;

    if (haystack == NULL || needle == NULL)
        return 0;

    needle_len = strlen(needle);

    if (needle_len == 0)
        return 1;

    for (p = haystack; *p != '\0'; ++p) {
        for (i = 0; i < needle_len; ++i) {
            if (p[i] == '\0')
                break;

            if (tolower((unsigned char)p[i]) !=
                tolower((unsigned char)needle[i]))
                break;
        }

        if (i == needle_len)
            return 1;
    }

    return 0;
}

/* ------------------------------------------------------------ */

#define UI_ROWS ui_index_rows
#define UI_BODY_ROWS ui_body_rows
#define UI_WIDTH ui_screen_width

static int ui_footer_row(int offset)
{
    return ui_screen_height - offset;
}

#define UI_KEY_UP   1072
#define UI_KEY_DOWN 1080
#define UI_KEY_HOME 1071
#define UI_KEY_END  1079
#define UI_KEY_PGUP 1073
#define UI_KEY_PGDN 1081
#define UI_KEY_F1   1059

static int ui_key(void)
{
    int ch = getch();

#ifdef __linux__
    switch (ch) {
    case KEY_UP:        return UI_KEY_UP;
    case KEY_DOWN:      return UI_KEY_DOWN;
    case KEY_HOME:      return UI_KEY_HOME;
    case KEY_END:       return UI_KEY_END;
    case KEY_PPAGE:     return UI_KEY_PGUP;
    case KEY_NPAGE:     return UI_KEY_PGDN;
    case KEY_RESIZE:
        ui_update_layout();
        return KEY_RESIZE;
    case KEY_F(1):      return UI_KEY_F1;
    case KEY_BACKSPACE: return 8;
    case 127:           return 8;
    default:            return ch;
    }
#else
    if (ch == 0 || ch == 0xE0)
        return 1000 + getch();

    return ch;
#endif
}

static int help_text(int row, int max_row,
                     const char *text, int attr, int indent)
{
    int width = UI_WIDTH;
    int text_width;
    int used = 0;
    char *line;
    const char *p = text;

    if (width < 1 || row >= max_row)
        return row;
    if (!text || !*text)
        return row + 1;

    if (indent < 0)
        indent = 0;
    if (indent >= width)
        indent = width - 1;

    text_width = width - indent;
    line = (char *)malloc((size_t)width + 1);
    if (!line)
        return row;

    while (*p && row < max_row) {
        const char *word;
        size_t word_len;

        while (*p == ' ' || *p == '\t')
            ++p;
        if (!*p)
            break;

        word = p;
        while (*p && *p != ' ' && *p != '\t')
            ++p;
        word_len = (size_t)(p - word);

        while (word_len > (size_t)text_width) {
            if (used) {
                line[indent + used] = '\0';
                write_vga_string(row++, 0, line, attr, width);
                used = 0;
                if (row >= max_row)
                    goto help_done;
            }

            memset(line, ' ', (size_t)indent);
            memcpy(line + indent, word, (size_t)text_width);
            line[indent + text_width] = '\0';
            write_vga_string(row++, 0, line, attr, width);

            word += text_width;
            word_len -= (size_t)text_width;
            if (row >= max_row)
                goto help_done;
        }

        if (used && used + 1 + (int)word_len > text_width) {
            line[indent + used] = '\0';
            write_vga_string(row++, 0, line, attr, width);
            used = 0;
            if (row >= max_row)
                goto help_done;
        }

        if (!used)
            memset(line, ' ', (size_t)indent);
        else
            line[indent + used++] = ' ';

        memcpy(line + indent + used, word, word_len);
        used += (int)word_len;
    }

    if (used && row < max_row) {
        line[indent + used] = '\0';
        write_vga_string(row++, 0, line, attr, width);
    }

help_done:
    free(line);
    return row;
}

static int ui_line(char *buf, size_t cap)
{
    size_t n = 0;
    int ch;
#ifndef __linux__
    int input_row = ui_footer_row(1);
    int input_col = (int)strlen("Find in topic: ");
#endif

    if (cap < 2)
        return 0;

#ifndef __linux__
    ui_set_cursor(input_row, input_col);
#endif

#ifdef __linux__
    refresh();
#endif

    for (;;) {
        ch = ui_key();

        if (ch == 27) {
            buf[0] = '\0';
#ifdef __linux__
            {
                move(getcury(stdscr), 0);
                clrtoeol();
                refresh();
            }
#endif
            return 0;
        }

        if (ch == '\r' || ch == '\n') {
            buf[n] = '\0';
#ifdef __linux__
            {
                move(getcury(stdscr), 0);
                clrtoeol();
                refresh();
            }
#endif
            return 1;
        }

        if (ch == 8 || ch == 127) {
            if (n > 0) {
                --n;
#ifdef __linux__
                int y, x;
                getyx(stdscr, y, x);
                if (x > 0) {
                    move(y, x - 1);
                    addch(' ');
                    move(y, x - 1);
                    refresh();
                }
#else
                write_vga_string(input_row, input_col + (int)n,
                                 " ", 0x4F, 1);
                ui_set_cursor(input_row, input_col + (int)n);
#endif
            }
            continue;
        }

        if (ch >= 32 && ch <= 126 && n + 1 < cap) {
            buf[n++] = (char)ch;
#ifdef __linux__
            addch(ch);
            refresh();
#else
            {
                char typed[2];
                typed[0] = (char)ch;
                typed[1] = '\0';
                write_vga_string(input_row, input_col + (int)n - 1,
                                 typed, 0x4F, 1);
                ui_set_cursor(input_row, input_col + (int)n);
            }
#endif
        }
    }
}

static void ui_wait(void)
{
    getch();
}

static int topic_matches(U32 i, const char *q)
{
    return contains_nocase(topics[i].id, q) ||
           contains_nocase(topics[i].title, q) ||
           contains_nocase(topics[i].category, q) ||
           contains_nocase(topics[i].source, q);
}

static void draw_index_row(const U32 *items, U32 count,
                           U32 selected, U32 row)
{
    U32 first, index;
    TOPIC *t;
    unsigned char attr;

    first = (selected / UI_ROWS) * UI_ROWS;
    index = first + row;

    {
        int width = UI_WIDTH;
        char *line;

        if (width < 1)
            return;

        line = (char *)malloc((size_t)width + 1);
        if (!line)
            return;

        memset(line, ' ', (size_t)width);
        line[width] = '\0';

        if (index < count) {
            int title_width = width - 22;
            int used;

            if (title_width < 0)
                title_width = 0;

            t = &topics[items[index]];
            used = snprintf(line, (size_t)width + 1,
                            "%c %5lu  %-12.12s %.*s",
                            index == selected ? '>' : ' ',
                            (unsigned long)(items[index] + 1),
                            t->id, title_width, t->title);

            if (used < 0)
                used = 0;
            if (used > width)
                used = width;
            if (used < width)
                memset(line + used, ' ', (size_t)(width - used));

            attr = (index == selected) ? 0x71 : 0x1F;
        } else {
            attr = 0x1F;
        }

        write_vga_string(3 + (int)row, 0, line, attr, width);
        free(line);
    }
}

static void draw_index(const U32 *items, U32 count, U32 selected,
                       const char *query)
{
    U32 first, end, row;

    first = (selected / UI_ROWS) * UI_ROWS;
    end = first + UI_ROWS;
    if (end > count)
        end = count;

    clear_screen();

    {
        int width = UI_WIDTH;
        char *line;

        if (width < 1)
            return;

        line = (char *)malloc((size_t)width + 1);
        if (!line)
            return;

        if (query[0])
            snprintf(line, (size_t)width + 1,
                     APP_CREDIT "  Search: %s", query);
        else
            snprintf(line, (size_t)width + 1, APP_CREDIT);
        write_vga_string(0, 0, line, 0x1E, width);

        if (count)
            snprintf(line, (size_t)width + 1,
                     "Item %lu of %lu | Database: %lu topics",
                     (unsigned long)(selected + 1),
                     (unsigned long)count, (unsigned long)topic_count);
        else
            snprintf(line, (size_t)width + 1,
                     "No matching topics | Database: %lu topics",
                     (unsigned long)topic_count);
        write_vga_string(1, 0, line, 0x1B, width);

        memset(line, '-', (size_t)width);
        line[width] = '\0';
        write_vga_string(2, 0, line, 0x19, width);

        free(line);
    }

    for (row = 0; row < (U32)UI_ROWS; ++row)
        draw_index_row(items, count, selected, row);

    {
        int width = UI_WIDTH;
        char *line = (char *)malloc((size_t)width + 1);

        if (line) {
            memset(line, '-', (size_t)width);
            line[width] = '\0';
            write_vga_string(ui_footer_row(3), 0, line, 0x19, width);
            free(line);
        }

        write_vga_string(ui_footer_row(2), 0,
                         "Up/Down Move  Home/End  PgUp/PgDn  Enter Open  S Search",
                         0x1E, width);
        write_vga_string(ui_footer_row(1), 0,
                         INDEX_FOOTER,
                         0x1E, width);
    }

    if (count > 0 && UI_ROWS > 0) {
        U32 page_first = (selected / UI_ROWS) * UI_ROWS;
        ui_set_cursor(3 + (int)(selected - page_first), 0);
    } else {
        ui_set_cursor(0, 0);
    }
}

static int make_body_lines(const char *body, U32 len,
                           U32 **out_lines, U32 *out_count)
{
    U32 i, count = 1, col = 0, n = 0;
    U32 *lines;

    for (i = 0; i < len; ++i) {
        unsigned char ch = (unsigned char)body[i];

        if (ch == '\r')
            continue;

        if (ch == '\n') {
            ++count;
            col = 0;
        } else {
            if (col >= (U32)UI_WIDTH) {
                ++count;
                col = 0;
            }
            ++col;
        }
    }

    lines = (U32 *)malloc((size_t)count * sizeof(U32));
    if (!lines)
        return 0;

    lines[n++] = 0;
    col = 0;

    for (i = 0; i < len; ++i) {
        unsigned char ch = (unsigned char)body[i];

        if (ch == '\r')
            continue;

        if (ch == '\n') {
            if (n < count)
                lines[n++] = i + 1;
            col = 0;
        } else {
            if (col >= (U32)UI_WIDTH) {
                if (n < count)
                    lines[n++] = i;
                col = 0;
            }
            ++col;
        }
    }

    *out_lines = lines;
    *out_count = n;
    return 1;
}

static void draw_body_line_vga(const char *body, U32 len,
                               U32 start, U32 next, int row)
{
    char *buf;
    int width = UI_WIDTH;
    int written = 0;
    U32 i;

    if (width < 1)
        return;

    buf = (char *)malloc((size_t)width + 1);
    if (!buf)
        return;

    memset(buf, ' ', (size_t)width);

    for (i = start; i < next && i < len && written < width; ++i) {
        unsigned char ch = (unsigned char)body[i];

        if (ch == '\r' || ch == '\n')
            break;

        if (ch == '\t') {
            int spaces = 8 - (written % 8);
            while (spaces-- > 0 && written < width)
                buf[written++] = ' ';
        } else {
            buf[written++] = ch >= 32 ? (char)ch : ' ';
        }
    }

    buf[width] = '\0';
    write_vga_string(row, 0, buf, 0x1F, width);
    free(buf);
}

static void show_topic(U32 number)
{
    TOPIC *t;
    char *body = NULL;
    U32 *lines = NULL;
    U32 line_count = 0, top = 0, i, next;
    char query[INPUT_SIZE];
    int ch, done = 0;
    size_t len;

    if (number >= topic_count)
        return;

load_topic:
    if (number >= topic_count)
        return;

    body = NULL;
    lines = NULL;
    line_count = 0;
    top = 0;
    done = 0;

    t = &topics[number];
    len = (size_t)t->rec.body_len;

    body = (char *)malloc(len + 1);
    if (!body) {
        printf("Not enough memory to open topic.\n");
        ui_wait();
        return;
    }

    if (fseek(db_file, (long)t->rec.body_off, SEEK_SET) != 0 ||
        fread(body, 1, len, db_file) != len) {
        free(body);
        printf("Could not read topic body.\n");
        ui_wait();
        return;
    }
    body[len] = '\0';

    if (!make_body_lines(body, (U32)len, &lines, &line_count)) {
        free(body);
        printf("Not enough memory to format topic.\n");
        ui_wait();
        return;
    }

    while (!done) {
        clear_screen();

        {
            int width = UI_WIDTH;
            char *hbuf = (char *)malloc((size_t)width + 1);
            if (hbuf) {
                snprintf(hbuf, (size_t)width + 1,
                         APP_TITLE " | %s | %lu/%lu",
                         t->id, (unsigned long)(number + 1),
                         (unsigned long)topic_count);
                write_vga_string(0, 0, hbuf, 0x1E, width);

                memset(hbuf, ' ', (size_t)width);
                hbuf[width] = '\0';
                {
                    size_t title_len = strlen(t->title);
                    if (title_len > (size_t)width)
                        title_len = (size_t)width;
                    memcpy(hbuf, t->title, title_len);
                }
                write_vga_string(1, 0, hbuf, 0x1F, width);

                memset(hbuf, '-', (size_t)width);
                hbuf[width] = '\0';
                write_vga_string(2, 0, hbuf, 0x19, width);
                free(hbuf);
            }
        }

        for (i = 0; i < (U32)UI_BODY_ROWS; ++i) {
            U32 li = top + i;

            if (li < line_count) {
                next = li + 1 < line_count ? lines[li + 1] : (U32)len;
                draw_body_line_vga(body, (U32)len, lines[li], next, 3 + (int)i);
            } else {
                write_vga_string(3 + (int)i, 0, "", 0x1F, UI_WIDTH);
            }
        }

        {
            int width = UI_WIDTH;
            char *fbuf = (char *)malloc((size_t)width + 1);
            if (fbuf) {
                memset(fbuf, '-', (size_t)width);
                fbuf[width] = '\0';
                write_vga_string(3 + UI_BODY_ROWS, 0, fbuf, 0x19, width);

                snprintf(fbuf, (size_t)width + 1,
                         "Line %lu/%lu  Up/Down Scroll  PgUp/PgDn Page  Home/End  / Find",
                         (unsigned long)(top + 1),
                         (unsigned long)line_count);
                write_vga_string(4 + UI_BODY_ROWS, 0, fbuf, 0x1E, width);

                snprintf(fbuf, (size_t)width + 1,
                         TOPIC_FOOTER);
                write_vga_string(5 + UI_BODY_ROWS, 0, fbuf, 0x1E, width);
                free(fbuf);
            }
        }

        ch = ui_key();

        switch (ch) {
        case UI_KEY_UP:
            if (top > 0) --top;
            break;
        case UI_KEY_DOWN:
            if (top + 1 < line_count) ++top;
            break;
        case UI_KEY_PGUP:
            top = top > (U32)UI_BODY_ROWS ? top - UI_BODY_ROWS : 0;
            break;
        case UI_KEY_PGDN:
            if (top + UI_BODY_ROWS < line_count)
                top += UI_BODY_ROWS;
            else
                top = line_count ? line_count - 1 : 0;
            break;
        case UI_KEY_HOME:
            top = 0;
            break;
        case UI_KEY_END:
            top = line_count > (U32)UI_BODY_ROWS ?
                  line_count - UI_BODY_ROWS : 0;
            break;
        case 27:
        case 'q':
        case 'Q':
            done = 1;
            break;
        case 'n':
        case 'N':
            if (number + 1 < topic_count) {
                free(lines);
                free(body);
                ++number;
                goto load_topic;
            }
            break;
        case 'p':
        case 'P':
            if (number > 0) {
                free(lines);
                free(body);
                --number;
                goto load_topic;
            }
            break;
        case '/':
            {
#ifdef __linux__
                move(ui_footer_row(1), 0);
                clrtoeol();
                attron(COLOR_PAIR(ui_colour_pair(0x4F)));
                addstr("Find in topic: ");
                attroff(COLOR_PAIR(ui_colour_pair(0x4F)));
                refresh();
#else
                write_vga_string(ui_footer_row(1), 0,
                                 "Find in topic:                                                                  ",
                                 0x4F, UI_WIDTH);
#endif
                if (ui_line(query, sizeof(query)) && query[0]) {
                    U32 found = line_count;
                    for (i = top; i < line_count; ++i) {
                        next = i + 1 < line_count ? lines[i + 1] : (U32)len;
                        {
                            char save = body[next < len ? next : len];
                            body[next < len ? next : len] = '\0';
                            if (contains_nocase(body + lines[i], query))
                                found = i;
                            body[next < len ? next : len] = save;
                        }
                        if (found != line_count)
                            break;
                    }
                    if (found < line_count)
                        top = found;
                }
            }
            break;
        default:
            break;
        }
    }

    free(lines);
    free(body);
}

static void list_topics(void)
{
    U32 *items;
    U32 count, selected = 0, i;
    char query[INPUT_SIZE] = "";
    char input[INPUT_SIZE];
    int searching = 0, done = 0, ch;
    int redraw = 1;
    U32 old_selected;

    items = (U32 *)malloc((size_t)topic_count * sizeof(U32));
    if (!items) {
        printf("Not enough memory for topic index.\n");
        ui_wait();
        return;
    }

    count = topic_count;
    for (i = 0; i < count; ++i)
        items[i] = i;

    while (!done) {
        if (count == 0)
            selected = 0;
        else if (selected >= count)
            selected = count - 1;

        if (redraw) {
            draw_index(items, count, selected, query);
            redraw = 0;
        }

        ch = ui_key();
        old_selected = selected;

        switch (ch) {
#ifdef __linux__
        case KEY_RESIZE:
            redraw = 1;
            break;
#endif
        case UI_KEY_UP:
            if (selected > 0) --selected;
            break;
        case UI_KEY_DOWN:
            if (selected + 1 < count) ++selected;
            break;
        case UI_KEY_HOME:
            selected = 0;
            break;
        case UI_KEY_END:
            if (count) selected = count - 1;
            break;
        case UI_KEY_PGUP:
            selected = selected > (U32)UI_ROWS ? selected - UI_ROWS : 0;
            break;
        case UI_KEY_PGDN:
            if (selected + UI_ROWS < count)
                selected += UI_ROWS;
            else if (count)
                selected = count - 1;
            break;
        case '\r':
        case '\n':
            if (count)
                show_topic(items[selected]);
            break;
        case '/':
        case 's':
        case 'S':
#ifdef __linux__
        {
            char saved_query[INPUT_SIZE];
            size_t n = 0;
            U32 saved_selected = selected;
            int saved_searching = searching;
            int finished = 0;

            strncpy(saved_query, query, sizeof(saved_query) - 1);
            saved_query[sizeof(saved_query) - 1] = '\0';
            input[0] = '\0';

            curs_set(1);

            while (!finished) {
                int prompt_len = (int)strlen("Search string: ");
                int cursor_x;

                {
                    int width = UI_WIDTH;
                    char *line = (char *)malloc((size_t)width + 1);

                    if (line) {
                        snprintf(line, (size_t)width + 1,
                                 APP_CREDIT "  Search: %s", input);
                        write_vga_string(0, 0, line, 0x1E, width);

                        if (count) {
                            snprintf(line, (size_t)width + 1,
                                     "Item %lu of %lu | Database: %lu topics",
                                     (unsigned long)(selected + 1),
                                     (unsigned long)count,
                                     (unsigned long)topic_count);
                        } else {
                            snprintf(line, (size_t)width + 1,
                                     "No matching topics | Database: %lu topics",
                                     (unsigned long)topic_count);
                        }
                        write_vga_string(1, 0, line, 0x1B, width);
                        free(line);
                    }

                    for (i = 0; i < (U32)UI_ROWS; ++i)
                        draw_index_row(items, count, selected, i);
                }

                move(ui_footer_row(1), 0);
                clrtoeol();
                attron(COLOR_PAIR(ui_colour_pair(0x4F)));
                addstr("Search string: ");
                attroff(COLOR_PAIR(ui_colour_pair(0x4F)));

                if (UI_WIDTH > prompt_len)
                    addnstr(input, UI_WIDTH - prompt_len);

                cursor_x = prompt_len + (int)n;
                if (cursor_x >= UI_WIDTH)
                    cursor_x = UI_WIDTH - 1;
                if (cursor_x < 0)
                    cursor_x = 0;

                move(ui_footer_row(1), cursor_x);
                refresh();

                ch = ui_key();

                if (ch == 27) {
                    strncpy(query, saved_query, sizeof(query) - 1);
                    query[sizeof(query) - 1] = '\0';
                    searching = saved_searching;

                    count = 0;
                    for (i = 0; i < topic_count; ++i) {
                        if (!searching || topic_matches(i, query))
                            items[count++] = i;
                    }

                    selected = count
                        ? (saved_selected < count ? saved_selected : count - 1)
                        : 0;
                    finished = 1;
                    continue;
                }

                if (ch == '\r' || ch == '\n') {
                    strncpy(query, input, sizeof(query) - 1);
                    query[sizeof(query) - 1] = '\0';
                    searching = query[0] != '\0';
                    finished = 1;
                    continue;
                }

                if (ch == KEY_RESIZE) {
                    draw_index(items, count, selected, input);
                    continue;
                }

                if (ch == 8 || ch == 127 || ch == KEY_BACKSPACE) {
                    if (n > 0) {
                        input[--n] = '\0';
                    }
                } else if (ch >= 32 && ch <= 126 &&
                           n + 1 < sizeof(input)) {
                    input[n++] = (char)ch;
                    input[n] = '\0';
                } else {
                    continue;
                }

                strncpy(query, input, sizeof(query) - 1);
                query[sizeof(query) - 1] = '\0';
                searching = query[0] != '\0';

                count = 0;
                for (i = 0; i < topic_count; ++i) {
                    if (!searching || topic_matches(i, query))
                        items[count++] = i;
                }
                selected = 0;
            }

            curs_set(0);
            redraw = 1;
        }
#else
        {
            char saved_query[INPUT_SIZE];
            char line[256];
            size_t n = 0;
            U32 saved_selected = selected;
            int saved_searching = searching;
            int finished = 0;
            int width = UI_WIDTH;

            strncpy(saved_query, query, sizeof(saved_query) - 1);
            saved_query[sizeof(saved_query) - 1] = '\0';
            input[0] = '\0';

            while (!finished) {
                if (width > 255)
                    width = 255;

                snprintf(line, sizeof(line),
                         APP_CREDIT "  Search: %s", input);
                write_vga_string(0, 0, line, 0x1E, width);

                if (count) {
                    snprintf(line, sizeof(line),
                             "Item %lu of %lu | Database: %lu topics",
                             (unsigned long)(selected + 1),
                             (unsigned long)count,
                             (unsigned long)topic_count);
                } else {
                    snprintf(line, sizeof(line),
                             "No matching topics | Database: %lu topics",
                             (unsigned long)topic_count);
                }
                write_vga_string(1, 0, line, 0x1B, width);

                memset(line, '-', (size_t)width);
                line[width] = '\0';
                write_vga_string(2, 0, line, 0x19, width);

                for (i = 0; i < (U32)UI_ROWS; ++i)
                    draw_index_row(items, count, selected, i);

                snprintf(line, sizeof(line), "Search string: %s", input);
                write_vga_string(ui_footer_row(1), 0, line, 0x4F, width);
                ui_set_cursor(ui_footer_row(1),
                              (int)strlen("Search string: ") + (int)n);

                ch = ui_key();

                if (ch == 27) {
                    strncpy(query, saved_query, sizeof(query) - 1);
                    query[sizeof(query) - 1] = '\0';
                    searching = saved_searching;

                    count = 0;
                    for (i = 0; i < topic_count; ++i) {
                        if (!searching || topic_matches(i, query))
                            items[count++] = i;
                    }

                    selected = count
                        ? (saved_selected < count ? saved_selected : count - 1)
                        : 0;
                    finished = 1;
                    continue;
                }

                if (ch == '\r' || ch == '\n') {
                    strncpy(query, input, sizeof(query) - 1);
                    query[sizeof(query) - 1] = '\0';
                    searching = query[0] != '\0';
                    finished = 1;
                    continue;
                }

                if (ch == 8 || ch == 127) {
                    if (n > 0)
                        input[--n] = '\0';
                } else if (ch >= 32 && ch <= 126 &&
                           n + 1 < sizeof(input)) {
                    input[n++] = (char)ch;
                    input[n] = '\0';
                } else {
                    continue;
                }

                strncpy(query, input, sizeof(query) - 1);
                query[sizeof(query) - 1] = '\0';
                searching = query[0] != '\0';

                count = 0;
                for (i = 0; i < topic_count; ++i) {
                    if (!searching || topic_matches(i, query))
                        items[count++] = i;
                }
                selected = 0;
            }

            redraw = 1;
        }
#endif
            break;
        case 'n':
        case 'N':
            if (searching && count) {
                for (i = selected + 1; i < count; ++i) {
                    if (topic_matches(items[i], query)) {
                        selected = i;
                        break;
                    }
                }
            } else if (selected + UI_ROWS < count) {
                selected += UI_ROWS;
            }
            break;
        case 'p':
        case 'P':
            if (searching && count) {
                if (selected > 0) --selected;
            } else {
                selected = selected > (U32)UI_ROWS ? selected - UI_ROWS : 0;
            }
            break;
        case 27:
            if (searching) {
                query[0] = '\0';
                searching = 0;
                count = topic_count;
                for (i = 0; i < count; ++i)
                    items[i] = i;
                selected = 0;
            }
            break;
        case 'q':
        case 'Q':
            done = 1;
            break;
        case UI_KEY_F1:
        {
            int row = 0;
            int max_row = ui_footer_row(1);

            clear_screen();
            write_vga_string(row++, 0,
                APP_TITLE " - Help and acknowledgements | Copyright (C) 2026 " APP_AUTHOR,
                0x1E, UI_WIDTH);

            row = help_text(row, max_row, "WHAT THIS PROGRAM DOES", 0x1E, 0);
            row = help_text(row, max_row,
                "RBIL QuickHelp is a searchable browser for Ralf Brown's "
                "Interrupt List: a classic PC reference covering interrupts, "
                "BIOS and DOS services, hardware interfaces and low-level "
                "software behaviour.", 0x1F, 2);

            row = help_text(row, max_row,
                "WITH DEEPEST THANKS TO RALF BROWN", 0x1E, 0);
            row = help_text(row, max_row,
                "Ralf Brown's Interrupt List is an extraordinary achievement "
                "of research, investigation and technical documentation. "
                "Its depth and detail reflect an immense amount of patient "
                "work: uncovering obscure interfaces, recording hard-won "
                "knowledge and preserving details that might otherwise have "
                "been lost. It has served generations of programmers as an "
                "indispensable guide to the inner workings of the PC. This "
                "viewer is a small effort to make that remarkable body of "
                "work easier to explore. The real achievement is the List "
                "itself, and this project is offered with profound gratitude "
                "and respect for Ralf Brown's dedication.", 0x1F, 2);

            row = help_text(row, max_row, "KEYBOARD CONTROLS", 0x1E, 0);
            row = help_text(row, max_row,
                "Up/Down  Move selection or scroll an article", 0x1F, 2);
            row = help_text(row, max_row,
                "Home/End  Go to the first or last topic/article position", 0x1F, 2);
            row = help_text(row, max_row,
                "PgUp/PgDn  Move one page", 0x1F, 2);
            row = help_text(row, max_row,
                "Enter  Open the selected topic", 0x1F, 2);
            row = help_text(row, max_row,
                "S  Search topic IDs, titles, categories and sources", 0x1F, 2);
            row = help_text(row, max_row,
                "N/P  Next/previous search match, or index page", 0x1F, 2);
            row = help_text(row, max_row,
                "Esc  Clear search or return from an article", 0x1F, 2);
            help_text(row, max_row, "Q  Quit", 0x1F, 2);

            write_vga_string(ui_footer_row(1), 0,
                             "Press any key to return...", 0x1E, UI_WIDTH);
            ui_wait();
            break;
        }
        default:
            break;
        }

        if (!done) {
            if (ch == UI_KEY_UP || ch == UI_KEY_DOWN) {
                if (selected != old_selected) {
                    if ((selected / UI_ROWS) ==
                        (old_selected / UI_ROWS)) {
                        draw_index_row(items, count, selected,
                                       old_selected % UI_ROWS);
                        draw_index_row(items, count, selected,
                                       selected % UI_ROWS);
                        ui_set_cursor(
                            3 + (int)(selected % UI_ROWS), 0);
                    } else {
                        redraw = 1;
                    }
                }
            } else {
                redraw = 1;
            }
        }
    }

    free(items);
}

int main(int argc, char **argv)
{
    const char *filename;

    if (argc < 2) {
        printf(APP_TITLE " Viewer\n\n");
        printf("Usage: RBILVIEW RBIL.RBD\n");
        return 1;
    }

    printf("RBILView\nParsing database, please wait a moment..\n");

    filename = argv[1];

    if (!load_database(filename)) {
        close_database();
        return 1;
    }

    printf("Loaded %lu topics from %s\n", (unsigned long)topic_count, filename);

    if (!ui_init()) {
        fprintf(stderr, "Could not initialise the user interface.\n");
        close_database();
        return 1;
    }

    list_topics();

    clear_screen();
    ui_shutdown();
    close_database();

    return 0;
}
