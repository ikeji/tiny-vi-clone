/*
 * vi-clone -- a tiny vi-like editor for 8086 MS-DOS.
 *
 * Target: LSI C-86 Ver 3.30 trial (small model).
 * Encoding: source is SJIS. Internal buffer holds SJIS bytes.
 * Terminal: ANSI.SYS / xterm compatible escape sequences.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#ifdef HOST_BUILD
/* Linux host build: use POSIX headers. jctype/conio don't exist, so
 * provide the iskanji macro over the SJIS lead-byte ranges. */
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#define iskanji(c) \
    (((unsigned char)(c) >= 0x81 && (unsigned char)(c) <= 0x9f) || \
     ((unsigned char)(c) >= 0xe0 && (unsigned char)(c) <= 0xfc))
#ifndef O_BINARY
#define O_BINARY 0
#endif
#else
#include <io.h>
#ifdef TEST_MAIN
/* LSI-C86 test build: no conio/jctype so we can drive input handlers
 * from C directly. Define just the iskanji macro. */
#define iskanji(c) \
    (((unsigned char)(c) >= 0x81 && (unsigned char)(c) <= 0x9f) || \
     ((unsigned char)(c) >= 0xe0 && (unsigned char)(c) <= 0xfc))
#else
#include <conio.h>
#include <jctype.h>
#endif
#endif

/* ----- limits ----- */
#define BUF_MAX   12288           /* bytes of text */
#define LINE_MAX  1024            /* max number of lines indexed */
#define SCREEN_W_DEFAULT 80
#define SCREEN_H_DEFAULT 25
#define SCREEN_W_MAX  132         /* upper bound for allocated line buffer */
#define CMD_MAX   64
#define MSG_MAX   80
#define VI_FNAME_MAX 64

/* Screen geometry, resolved at startup from LINES/COLUMNS env vars
 * (or the defaults). TEXT_H is rows - 2 (status + cmdline). */
static int SCREEN_W = SCREEN_W_DEFAULT;
static int SCREEN_H = SCREEN_H_DEFAULT;
static int TEXT_H   = SCREEN_H_DEFAULT - 2;

/* Resolve SCREEN_W/H from the environment. VI_COLS / VI_ROWS take
 * priority, then COLUMNS / LINES (POSIX), then the 80x25 defaults.
 * Values outside sane bounds are ignored. */
static void init_screen_size(void)
{
    const char *s;
    int v;
    s = getenv("VI_COLS");
    if (!s || !*s) s = getenv("COLUMNS");
    if (s && *s) {
        v = atoi(s);
        if (v >= 20 && v <= SCREEN_W_MAX) SCREEN_W = v;
    }
    s = getenv("VI_ROWS");
    if (!s || !*s) s = getenv("LINES");
    if (s && *s) {
        v = atoi(s);
        if (v >= 5 && v <= 200) SCREEN_H = v;
    }
    TEXT_H = SCREEN_H - 2;
    if (TEXT_H < 1) TEXT_H = 1;
}

/* ----- buffer state ----- */
static char buf[BUF_MAX];
static int  buf_len;              /* bytes currently in buf */
static int  line_ofs[LINE_MAX + 1]; /* offset of each line start; +1 holds buf_len */
static int  line_count;           /* number of lines (>= 1) */

/* cursor: byte offset in buf */
static int  cur_off;

/* sticky desired column for vertical motion. -1 means "use current col". */
static int  desired_col;

/* viewport: top line shown on screen */
static int  top_line;

/* file info */
static char filename[VI_FNAME_MAX];
static int  dirty;

/* modes */
#define MODE_NORMAL  0
#define MODE_INSERT  1
#define MODE_CMDLINE 2
#define MODE_TUT     3
static int mode;

/* TUT-code input buffer (hiragana max 4-char sequences like "dldu"=づ) */
#define TUT_BUF_MAX 6
static char tut_buf[TUT_BUF_MAX];
static int  tut_len;

/* pending operator (e.g. 'd' waiting for second char) */
static int pending_op;

/* status message to show on next redraw */
static char msg[MSG_MAX];

/* command-line (ex) buffer */
static char cmdline[CMD_MAX];
static int  cmdline_len;

/* ----- redraw dirty tracking -----
 * To avoid a slow full redraw on every keystroke (seconds per key on real
 * 8086 hardware), track what actually changed. Edit primitives set these
 * flags; draw_all() decides what subset of the screen to rewrite.
 *
 *  dirty_from  -- smallest buffer line number that needs redrawing; -1 = none
 *  dirty_all   -- if nonzero, redraw dirty_from..bottom of viewport (lines
 *                 shifted, e.g. after inserting/deleting a '\n')
 *  need_full   -- force a complete viewport + clear (initial frame, explicit)
 *  last_top    -- top_line value at the previous draw; used to detect scroll
 */
static int  dirty_from = -1;
static int  dirty_all  = 0;
static int  need_full  = 1;
static int  last_top   = -1;

static void mark_line_dirty(int ln)
{
    if (dirty_from < 0 || ln < dirty_from) dirty_from = ln;
}
static void mark_below_dirty(int ln)
{
    mark_line_dirty(ln);
    dirty_all = 1;
}

/* ----- forward ----- */
static void reindex(void);
static int  line_of(int off);
static int  line_start(int ln);
static int  line_end(int ln);  /* offset of the '\n' or buf_len if last */
static int  clamp_offset(int off);
static int  byte_is_lead(int off);
static int  prev_byte(int off);
static int  next_byte(int off);
static void draw_all(void);
static void redraw_status(void);

/* Virtual key codes for extended (arrow) keys. Defined unconditionally
 * so input handlers can be compiled for test builds without conio. */
#define KEY_EXT_BASE 0x100
#define KEY_UP    (KEY_EXT_BASE + 0x48)
#define KEY_DOWN  (KEY_EXT_BASE + 0x50)
#define KEY_LEFT  (KEY_EXT_BASE + 0x4B)
#define KEY_RIGHT (KEY_EXT_BASE + 0x4D)

/* ================================================================
 * Terminal I/O
 * ================================================================ */

#ifndef TEST_MAIN

#ifdef HOST_BUILD

/* On the host build, putch/getch aren't in libc. Provide shims that
 * write to stdout directly and read raw bytes from stdin (termios raw). */

static void host_putc(int c)
{
    unsigned char b = (unsigned char)c;
    write(1, &b, 1);
}

static int host_getc(void)
{
    unsigned char b;
    ssize_t n = read(0, &b, 1);
    if (n != 1) return -1;
    return b;
}

/* Try to read another byte within a short timeout. -1 if nothing arrived. */
static int host_getc_timeout(int ms)
{
    fd_set fds;
    struct timeval tv;
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    tv.tv_sec = 0;
    tv.tv_usec = ms * 1000;
    if (select(1, &fds, NULL, NULL, &tv) <= 0) return -1;
    return host_getc();
}

#define putch(c) host_putc(c)

#endif /* HOST_BUILD */

/* Write string with direct output so we don't depend on stdout
 * buffering semantics while cursor-positioning. */
static void tputs(const char *s)
{
    while (*s) putch(*s++);
}

static void tprintf_num(int n)
{
    char t[8];
    int i = 0;
    if (n < 0) { putch('-'); n = -n; }
    if (n == 0) { putch('0'); return; }
    while (n > 0) { t[i++] = (char)('0' + n % 10); n /= 10; }
    while (i > 0) putch(t[--i]);
}

/* Move cursor to (row, col), both 1-based. */
static void tgoto(int row, int col)
{
    putch(0x1b); putch('[');
    tprintf_num(row);
    putch(';');
    tprintf_num(col);
    putch('H');
}

static void tclear(void)        { tputs("\x1b[2J"); tgoto(1, 1); }
static void tclr_eol(void)      { tputs("\x1b[K"); }
static void tinvert_on(void)    { tputs("\x1b[7m"); }
static void tinvert_off(void)   { tputs("\x1b[0m"); }

/* Read one key. On DOS, extended keys arrive as 0x00/0xE0 + scancode.
 * On a host xterm, arrow keys arrive as ESC [ A/B/C/D. Both are
 * translated to KEY_EXT_BASE + (DOS scancode) so normal_key() can
 * share the case labels. */
static int key_read(void)
{
#ifdef HOST_BUILD
    int c = host_getc();
    if (c < 0) return 0x1b;  /* treat stdin close as ESC */
    if (c != 0x1b) return c;
    /* Possible ESC sequence. If no more bytes quickly, it's a real ESC. */
    {
        int b = host_getc_timeout(30);
        if (b != '[' && b != 'O') {
            /* standalone ESC (or unknown prefix) */
            return 0x1b;
        }
        b = host_getc_timeout(30);
        switch (b) {
        case 'A': return KEY_UP;
        case 'B': return KEY_DOWN;
        case 'C': return KEY_RIGHT;
        case 'D': return KEY_LEFT;
        default:  return 0x1b;
        }
    }
#else
    int c = (unsigned char)getch();
    if (c == 0x00 || c == 0xE0) {
        int s = (unsigned char)getch();
        return KEY_EXT_BASE + s;
    }
    return c;
#endif
}

#endif /* !TEST_MAIN */

/* ================================================================
 * Buffer helpers
 * ================================================================ */

static void reindex(void)
{
    int i;
    line_count = 0;
    line_ofs[line_count++] = 0;
    for (i = 0; i < buf_len; i++) {
        if (buf[i] == '\n') {
            if (line_count >= LINE_MAX) break;
            line_ofs[line_count++] = i + 1;
        }
    }
    line_ofs[line_count] = buf_len;
}

static int line_of(int off)
{
    int lo = 0, hi = line_count - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (line_ofs[mid] <= off) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

static int line_start(int ln) { return line_ofs[ln]; }

static int line_end(int ln)
{
    if (ln + 1 < line_count) return line_ofs[ln + 1] - 1; /* points at '\n' */
    return buf_len; /* past-end for last line without trailing newline */
}

static int line_byte_len(int ln)
{
    return line_end(ln) - line_start(ln);
}

static int byte_is_lead(int off)
{
    /* A byte is a SJIS lead byte iff it is a kanji-range byte AND the
     * count of consecutive kanji-range bytes from the start of the line
     * up to and including this position is odd. (This preserves SJIS
     * framing even when the second byte happens to also be in the lead
     * range.) */
    int ls, i, toggles;
    if (off < 0 || off >= buf_len) return 0;
    if (!iskanji((unsigned char)buf[off])) return 0;
    ls = line_start(line_of(off));
    toggles = 0;
    for (i = ls; i <= off; i++) {
        if (!iskanji((unsigned char)buf[i])) { toggles = 0; continue; }
        toggles = !toggles;
    }
    return toggles;
}

static int prev_byte(int off)
{
    if (off <= 0) return 0;
    off--;
    /* If we just landed on a SJIS trail byte, step back one more. */
    if (off > 0 && byte_is_lead(off - 1) && buf[off - 1] != '\n') {
        /* The byte at off-1 is a lead => off is its trail. But we want
         * to be positioned at the character start, which is off-1. */
        off--;
    }
    return off;
}

static int next_byte(int off)
{
    if (off >= buf_len) return buf_len;
    if (byte_is_lead(off)) off += 2;
    else off += 1;
    if (off > buf_len) off = buf_len;
    return off;
}

static int clamp_offset(int off)
{
    int ln, ls, le;
    if (off < 0) off = 0;
    if (off > buf_len) off = buf_len;
    ln = line_of(off);
    ls = line_start(ln);
    le = line_end(ln);
    /* Don't sit on the '\n'. If line is empty, sit on ls. */
    if (off > le) off = le;
    if (mode == MODE_NORMAL && off > ls && off == le && le > ls) {
        /* in normal mode, cursor is on a real char, not past end */
        /* step back, honoring SJIS */
        off = prev_byte(off);
        if (off < ls) off = ls;
    }
    return off;
}

/* ================================================================
 * Editing primitives
 * ================================================================ */

/* Insert a byte at off. Returns new cursor position (off+1). */
static int buf_insert_byte(int off, char c)
{
    int ln;
    if (buf_len >= BUF_MAX) return off;
    if (off < 0) off = 0;
    if (off > buf_len) off = buf_len;
    ln = line_of(off);
    memmove(&buf[off + 1], &buf[off], buf_len - off);
    buf[off] = c;
    buf_len++;
    dirty = 1;
    reindex();
    /* '\n' shifts every line below down by one row → redraw from here */
    if (c == '\n') mark_below_dirty(ln);
    else           mark_line_dirty(ln);
    return off + 1;
}

/* Delete n bytes starting at off. Returns off (unchanged). */
static int buf_delete_bytes(int off, int n)
{
    int ln, had_nl, i;
    if (off < 0) off = 0;
    if (off >= buf_len) return off;
    if (off + n > buf_len) n = buf_len - off;
    if (n <= 0) return off;
    ln = line_of(off);
    /* Any '\n' in the deleted span shifts subsequent lines up. */
    had_nl = 0;
    for (i = 0; i < n; i++) if (buf[off + i] == '\n') { had_nl = 1; break; }
    memmove(&buf[off], &buf[off + n], buf_len - off - n);
    buf_len -= n;
    dirty = 1;
    reindex();
    if (had_nl) mark_below_dirty(ln);
    else        mark_line_dirty(ln);
    return off;
}

/* Delete the current char (1 or 2 bytes for SJIS). */
static void do_delete_char(void)
{
    int n = 1;
    int ln, le;
    if (cur_off >= buf_len) return;
    if (buf[cur_off] == '\n') return; /* 'x' on empty line: no-op */
    if (byte_is_lead(cur_off) && cur_off + 1 < buf_len) n = 2;
    cur_off = buf_delete_bytes(cur_off, n);
    ln = line_of(cur_off);
    le = line_end(ln);
    if (cur_off >= le && le > line_start(ln)) cur_off = prev_byte(le);
    cur_off = clamp_offset(cur_off);
}

/* Delete an entire line. */
static void do_delete_line(void)
{
    int ln = line_of(cur_off);
    int ls = line_start(ln);
    int n;
    if (ln + 1 < line_count) {
        n = line_ofs[ln + 1] - ls;  /* includes the trailing '\n' */
    } else if (line_count > 1) {
        /* deleting last line; also remove preceding '\n' */
        n = buf_len - ls;
        ls = line_ofs[ln] - 1;  /* include previous '\n' */
        n = buf_len - ls;
    } else {
        /* only one line; clear it */
        n = buf_len;
        ls = 0;
    }
    cur_off = buf_delete_bytes(ls, n);
    /* if we deleted the last line, position at start of new last line */
    if (cur_off >= buf_len) {
        cur_off = line_start(line_count - 1);
    }
    cur_off = clamp_offset(cur_off);
}

/* Insert a newline at cursor (used by 'o', 'O', and CR in insert). */
static void do_insert_newline(void)
{
    cur_off = buf_insert_byte(cur_off, '\n');
}

/* Insert an arbitrary byte in insert mode. */
static void do_insert_byte(char c)
{
    cur_off = buf_insert_byte(cur_off, c);
}

/* Backspace in insert mode: delete the byte (or 2-byte char) to the left. */
static void do_insert_backspace(void)
{
    int nb;
    if (cur_off <= 0) return;
    nb = prev_byte(cur_off);
    cur_off = buf_delete_bytes(nb, cur_off - nb);
}

/* ================================================================
 * Motion
 * ================================================================ */

static void move_left(void)
{
    int ln = line_of(cur_off);
    int ls = line_start(ln);
    desired_col = -1;
    if (cur_off <= ls) return;
    cur_off = prev_byte(cur_off);
    if (cur_off < ls) cur_off = ls;
}

static void move_right(void)
{
    int ln = line_of(cur_off);
    int le = line_end(ln);
    int nx = next_byte(cur_off);
    desired_col = -1;
    if (nx >= le) {
        /* stay on last real char; normal mode doesn't sit past end */
        if (le > line_start(ln)) cur_off = prev_byte(le);
        else cur_off = le;
        return;
    }
    cur_off = nx;
}

/* Column in screen cells for a given offset, computed from line start. */
static int col_of(int off)
{
    int ls = line_start(line_of(off));
    int i = ls, col = 0;
    while (i < off) {
        if (byte_is_lead(i) && i + 1 < buf_len && buf[i + 1] != '\n') {
            col += 2; i += 2;
        } else {
            col += 1; i += 1;
        }
    }
    return col;
}

/* Find offset in line ln corresponding to target display column,
 * rounding down at SJIS boundaries. */
static int offset_at_col(int ln, int target_col)
{
    int ls = line_start(ln), le = line_end(ln);
    int i = ls, col = 0;
    while (i < le) {
        int w;
        if (byte_is_lead(i) && i + 1 < le) w = 2;
        else w = 1;
        if (col + w > target_col) break;
        col += w; i += w;
    }
    return i;
}

static void move_up(void)
{
    int ln = line_of(cur_off);
    int target;
    if (ln == 0) return;
    target = (desired_col >= 0) ? desired_col : col_of(cur_off);
    if (desired_col < 0) desired_col = target;
    cur_off = offset_at_col(ln - 1, target);
    cur_off = clamp_offset(cur_off);
}

static void move_down(void)
{
    int ln = line_of(cur_off);
    int target;
    if (ln + 1 >= line_count) return;
    target = (desired_col >= 0) ? desired_col : col_of(cur_off);
    if (desired_col < 0) desired_col = target;
    cur_off = offset_at_col(ln + 1, target);
    cur_off = clamp_offset(cur_off);
}

static void move_home(void)
{
    cur_off = line_start(line_of(cur_off));
    desired_col = -1;
}

static void move_end(void)
{
    int ln = line_of(cur_off);
    int le = line_end(ln);
    if (le > line_start(ln)) cur_off = prev_byte(le);
    else cur_off = le;
    desired_col = -1;
}

static void move_top(void) { cur_off = 0; desired_col = -1; }

static void move_bottom(void)
{
    cur_off = line_start(line_count - 1);
    desired_col = -1;
}

/* ================================================================
 * File I/O
 * ================================================================ */

/* Load filename into buf. Returns 0 on success, -1 on error.
 * If the file doesn't exist, returns 0 with an empty buffer. */
static int file_load(const char *name)
{
    int fd, n;
    char *p, *q;
    buf_len = 0;
    if (!name || !*name) { reindex(); return 0; }
    fd = open(name, O_RDONLY
#ifdef O_BINARY
                  | O_BINARY
#endif
             );
    if (fd < 0) { reindex(); return 0; }
    n = read(fd, buf, BUF_MAX);
    close(fd);
    if (n < 0) { reindex(); return -1; }
    /* Normalize CRLF -> LF. */
    p = buf; q = buf;
    while (p < buf + n) {
        if (p[0] == '\r' && p + 1 < buf + n && p[1] == '\n') {
            *q++ = '\n';
            p += 2;
        } else {
            *q++ = *p++;
        }
    }
    buf_len = (int)(q - buf);
    dirty = 0;
    reindex();
    return 0;
}

/* Save buf to filename. Returns 0 on success, -1 on error. */
static int file_save(const char *name)
{
    int fd, i, wrote;
    char crlf[2];
    if (!name || !*name) return -1;
    fd = open(name, O_WRONLY | O_CREAT | O_TRUNC
#ifdef O_BINARY
                   | O_BINARY
#endif
             , 0666);
    if (fd < 0) return -1;
    crlf[0] = '\r'; crlf[1] = '\n';
    wrote = 0;
    for (i = 0; i < buf_len; i++) {
        if (buf[i] == '\n') {
            if (write(fd, crlf, 2) != 2) { close(fd); return -1; }
            wrote += 2;
        } else {
            if (write(fd, &buf[i], 1) != 1) { close(fd); return -1; }
            wrote++;
        }
    }
    close(fd);
    dirty = 0;
    return 0;
}

/* ================================================================
 * TUT-Code input
 * ================================================================
 *
 * A small subset of the TUT-Code 2-3 stroke hiragana/katakana table.
 * Source: ibus-tutcode engine/tutcode_rule.py (GPL-compatible reference
 * material). Only kana that map to a single SJIS double-byte code are
 * listed here; multi-byte output sequences are not supported.
 *
 * The sequence column is ASCII letters; the code column is the SJIS
 * double-byte value with the lead byte in the high byte.
 */

typedef struct {
    const char    *seq;
    unsigned short code;
} tut_entry;

/* Table generated from ibus-tutcode. Covers hiragana + katakana + kanji
 * (about 2611 entries). See build/gen_tut_table.py. */
#include "tuttable.h"

/* Try to match the current tut_buf. Returns the matching code, or 0 if
 * no exact match. *is_prefix is set nonzero if at least one entry has
 * tut_buf as a proper prefix (so we should wait for more input). */
static unsigned short tut_lookup(int *is_prefix)
{
    const tut_entry *e;
    *is_prefix = 0;
    if (tut_len == 0) return 0;
    for (e = tut_table; e->seq; e++) {
        if (strncmp(e->seq, tut_buf, tut_len) == 0) {
            if (e->seq[tut_len] == '\0') return e->code;
            *is_prefix = 1;
        }
    }
    return 0;
}

static void tut_reset(void)
{
    tut_len = 0;
    tut_buf[0] = '\0';
}

static void tut_commit(unsigned short code)
{
    do_insert_byte((char)((code >> 8) & 0xff));
    do_insert_byte((char)(code & 0xff));
}

/* Handle a key while in TUT mode. Returns void; mode may change. */
static void tut_key(int k)
{
    int is_prefix;
    unsigned short code;

    if (k == 0x1b) {                       /* ESC: leave TUT */
        tut_reset();
        mode = MODE_INSERT;
        return;
    }
    if (k == 0x08 || k == 0x7f) {          /* BS */
        if (tut_len > 0) {
            /* in the middle of a stroke: undo the last keystroke */
            tut_len--;
            tut_buf[tut_len] = '\0';
        } else {
            /* no pending stroke: delete the previous char in the buffer,
             * honoring SJIS 2-byte boundaries */
            do_insert_backspace();
        }
        return;
    }
    if (k == 0x0d) {                       /* CR: insert newline, stay in TUT */
        tut_reset();
        do_insert_newline();
        return;
    }
    if (!((k >= 'a' && k <= 'z') ||
          k == ',' || k == '.' || k == ';' || k == '/')) {
        /* non-stroke key cancels the sequence but stays in TUT mode */
        tut_reset();
        return;
    }
    if (tut_len >= TUT_BUF_MAX - 1) {
        tut_reset();
        return;
    }
    tut_buf[tut_len++] = (char)k;
    tut_buf[tut_len] = '\0';

    code = tut_lookup(&is_prefix);
    if (code) {
        tut_commit(code);
        tut_reset();
        return;
    }
    if (!is_prefix) {
        /* dead sequence: drop silently */
        tut_reset();
    }
}

/* ================================================================
 * ex command parser
 * ================================================================ */

/* Execute the contents of cmdline. Returns 1 to quit editor, 0 otherwise. */
static int ex_execute(void)
{
    int force = 0;
    char *p = cmdline;
    int write_req = 0, quit_req = 0;

    while (*p == ' ') p++;
    /* Parse leading letters w/q and optional ! */
    while (*p == 'w' || *p == 'q') {
        if (*p == 'w') write_req = 1;
        if (*p == 'q') quit_req = 1;
        p++;
    }
    if (*p == '!') { force = 1; p++; }
    while (*p == ' ') p++;

    if (!write_req && !quit_req) {
        strcpy(msg, "unknown command");
        return 0;
    }

    if (write_req) {
        const char *name = filename;
        if (*p) name = p; /* :w newname */
        if (file_save(name) < 0) {
            strcpy(msg, "write failed");
            return 0;
        }
        if (name != filename) {
            strncpy(filename, name, VI_FNAME_MAX - 1);
            filename[VI_FNAME_MAX - 1] = '\0';
        }
        sprintf(msg, "\"%s\" written", filename);
    }

    if (quit_req) {
        if (dirty && !force && !write_req) {
            strcpy(msg, "no write since last change (add ! to override)");
            return 0;
        }
        return 1;
    }
    return 0;
}

/* ================================================================
 * Screen drawing
 * ================================================================ */

#ifndef TEST_MAIN

static void draw_line(int screen_row, int ln)
{
    int ls, le, i, col;
    tgoto(screen_row, 1);
    tclr_eol();
    if (ln >= line_count) {
        putch('~');
        return;
    }
    ls = line_start(ln);
    le = line_end(ln);
    col = 0;
    for (i = ls; i < le; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c == '\t') {
            int spaces = 8 - (col & 7);
            while (spaces-- > 0 && col < SCREEN_W) { putch(' '); col++; }
            continue;
        }
        if (col >= SCREEN_W) break;
        if (byte_is_lead(i) && i + 1 < le && col + 2 <= SCREEN_W) {
            putch(buf[i]);
            putch(buf[i + 1]);
            i++;
            col += 2;
        } else if (c >= 0x20 || c == '\0') {
            putch(c ? (char)c : ' ');
            col++;
        } else {
            putch('?');
            col++;
        }
    }
}

static void redraw_status(void)
{
    int ln = line_of(cur_off);
    int co = col_of(cur_off);
    char line[SCREEN_W_MAX + 1];
    int n;

    tgoto(SCREEN_H - 1, 1);
    tclr_eol();
    if (mode == MODE_CMDLINE) {
        putch(':');
        for (n = 0; n < cmdline_len && n < SCREEN_W - 1; n++)
            putch(cmdline[n]);
    } else if (msg[0]) {
        for (n = 0; msg[n] && n < SCREEN_W; n++) putch(msg[n]);
    }

    tgoto(SCREEN_H, 1);
    tclr_eol();
    {
        const char *mode_str = "";
        char tut_disp[24];
        if (mode == MODE_INSERT) mode_str = "-- INSERT --";
        else if (mode == MODE_TUT) {
            sprintf(tut_disp, "-- TUT [%s] --", tut_buf);
            mode_str = tut_disp;
        }
        n = sprintf(line, "%s%s %d,%d %s",
                    filename[0] ? filename : "[no name]",
                    dirty ? " [+]" : "",
                    ln + 1, co + 1,
                    mode_str);
    }
    (void)n;
    {
        int k;
        for (k = 0; line[k] && k < SCREEN_W; k++) putch(line[k]);
    }
}

static void redraw_cursor(void)
{
    int ln = line_of(cur_off);
    int co = col_of(cur_off);
    int row = ln - top_line + 1;
    if (row < 1) row = 1;
    if (row > TEXT_H) row = TEXT_H;
    if (mode == MODE_CMDLINE) {
        tgoto(SCREEN_H - 1, cmdline_len + 2);
    } else {
        tgoto(row, co + 1);
    }
}

static void ensure_visible(void)
{
    int ln = line_of(cur_off);
    if (ln < top_line) top_line = ln;
    if (ln >= top_line + TEXT_H) top_line = ln - TEXT_H + 1;
}

/* Redraw only what the edit primitives marked dirty. Falls back to a
 * full viewport rewrite when:
 *   - this is the first frame (need_full)
 *   - the viewport scrolled since the last draw (top_line changed)
 *   - an edit set dirty_all (line-count changed, lines shifted)
 * The status line is cheap enough that we always rewrite it; the
 * expensive part is the TEXT_H text rows. */
static void draw_all(void)
{
    int first, last, i;

    if (need_full || top_line != last_top) {
        /* Full viewport redraw. Use tclear() only on the very first frame
         * so we don't repaint blank space unnecessarily. */
        if (need_full) tclear();
        for (i = 0; i < TEXT_H; i++) draw_line(i + 1, top_line + i);
    } else if (dirty_from >= 0) {
        first = dirty_from;
        if (first < top_line) first = top_line;
        last = dirty_all ? (top_line + TEXT_H - 1) : first;
        if (last >= top_line + TEXT_H) last = top_line + TEXT_H - 1;
        for (i = first; i <= last; i++) {
            draw_line(i - top_line + 1, i);
        }
    }

    /* Status (2 lines) -- cheap, always rewrite. */
    redraw_status();
    redraw_cursor();

    dirty_from = -1;
    dirty_all  = 0;
    need_full  = 0;
    last_top   = top_line;
}

#endif /* !TEST_MAIN (screen drawing) */

/* ================================================================
 * Input handlers (state-only; no conio calls)
 * ================================================================ */

/* Returns 1 if the editor should exit after this call (ZZ quit). */
static int normal_key(int k)
{
    msg[0] = '\0';
    if (pending_op == 'd') {
        pending_op = 0;
        if (k == 'd') { do_delete_line(); return 0; }
        return 0;
    }
    if (pending_op == 'g') {
        pending_op = 0;
        if (k == 'g') { move_top(); return 0; }
        return 0;
    }
    if (pending_op == 'Z') {
        pending_op = 0;
        if (k == 'Z') {
            /* ZZ: save and quit, like :wq */
            if (!filename[0]) {
                strcpy(msg, "no file name");
                return 0;
            }
            if (file_save(filename) < 0) {
                strcpy(msg, "write failed");
                return 0;
            }
            return 1;
        }
        return 0;
    }
    switch (k) {
    case 'h': case KEY_LEFT:  move_left(); break;
    case 'l': case KEY_RIGHT: move_right(); break;
    case 'k': case KEY_UP:    move_up(); break;
    case 'j': case KEY_DOWN:  move_down(); break;
    case '0':                 move_home(); break;
    case '$':                 move_end(); break;
    case 'G':                 move_bottom(); break;
    case 'g':                 pending_op = 'g'; break;
    case 'Z':                 pending_op = 'Z'; break;
    case 'x':                 do_delete_char(); break;
    case 'd':                 pending_op = 'd'; break;
    case 'i':                 mode = MODE_INSERT; break;
    case 'a':
        if (cur_off < line_end(line_of(cur_off))) cur_off = next_byte(cur_off);
        mode = MODE_INSERT; break;
    case 'A':
        move_end();
        if (cur_off < line_end(line_of(cur_off))) cur_off = next_byte(cur_off);
        mode = MODE_INSERT; break;
    case 'I':
        move_home(); mode = MODE_INSERT; break;
    case 'o':
        move_end();
        if (cur_off < line_end(line_of(cur_off))) cur_off = next_byte(cur_off);
        do_insert_newline();
        mode = MODE_INSERT; break;
    case 'O':
        move_home();
        buf_insert_byte(cur_off, '\n');
        /* cur_off is now after the \n; but for 'O' we want to be on the new
         * blank line above. The insert put '\n' at cur_off-1; we want to be
         * at the position where the new line begins, which is the old
         * cur_off (now the second line's start - 1 shift). Simpler: step
         * back. */
        cur_off--;
        reindex();
        mode = MODE_INSERT; break;
    case ':':
        mode = MODE_CMDLINE;
        cmdline_len = 0;
        cmdline[0] = '\0';
        break;
    case 0x1b: /* ESC in normal: cancel pending op */
        pending_op = 0;
        break;
    default:
        /* unknown */
        break;
    }
    return 0;
}

static void insert_key(int k)
{
    if (k == 0x1b) {
        mode = MODE_NORMAL;
        if (cur_off > line_start(line_of(cur_off))) cur_off = prev_byte(cur_off);
        cur_off = clamp_offset(cur_off);
        return;
    }
    if (k == 0x0a) {                /* Ctrl+J: enter TUT input mode */
        tut_reset();
        mode = MODE_TUT;
        return;
    }
    if (k == 0x08 || k == 0x7f) { do_insert_backspace(); return; }
    if (k == 0x0d) { do_insert_newline(); return; }
    if (k >= 0x20 && k < 0x100) { do_insert_byte((char)k); return; }
    /* arrow keys ignored in insert */
}

static void cmdline_key(int k)
{
    if (k == 0x1b) {
        mode = MODE_NORMAL; cmdline_len = 0; return;
    }
    if (k == 0x0d || k == 0x0a) {
        cmdline[cmdline_len] = '\0';
        mode = MODE_NORMAL;
        /* execute; caller checks return */
        return;
    }
    if (k == 0x08 || k == 0x7f) {
        if (cmdline_len > 0) cmdline_len--;
        return;
    }
    if (k >= 0x20 && k < 0x7f && cmdline_len < CMD_MAX - 1) {
        cmdline[cmdline_len++] = (char)k;
    }
}

#ifndef TEST_MAIN

#ifdef HOST_BUILD

static struct termios saved_termios;
static int termios_saved;

static void restore_termios(void)
{
    if (termios_saved) {
        tcsetattr(0, TCSANOW, &saved_termios);
    }
}

static void setup_termios(void)
{
    struct termios t;
    if (tcgetattr(0, &saved_termios) < 0) return;
    termios_saved = 1;
    t = saved_termios;
    /* raw: no echo, no canonical, no signal chars (so we get raw ^C),
     * no CR->NL translation on input. */
    t.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    t.c_iflag &= ~(ICRNL | INLCR | IGNCR | IXON | ISTRIP | BRKINT);
    t.c_oflag &= ~(OPOST);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &t);
    atexit(restore_termios);
}

#endif /* HOST_BUILD */

/* ================================================================
 * main
 * ================================================================ */

int main(int argc, char **argv)
{
    int k;

    init_screen_size();

#ifdef HOST_BUILD
    if (!isatty(0) || !isatty(1)) {
        fprintf(stderr, "vi-clone: stdin/stdout must be a terminal\n");
        return 1;
    }
    setup_termios();
#endif

    filename[0] = '\0';
    if (argc >= 2) {
        strncpy(filename, argv[1], VI_FNAME_MAX - 1);
        filename[VI_FNAME_MAX - 1] = '\0';
        if (file_load(filename) < 0) {
            fprintf(stderr, "cannot read %s\n", filename);
            return 1;
        }
    } else {
        reindex();
    }

    mode = MODE_NORMAL;
    cur_off = 0;
    top_line = 0;
    msg[0] = '\0';

    /* draw_all() clears on the first frame via need_full=1, so no
     * redundant tclear() here. */
    ensure_visible();
    draw_all();

    for (;;) {
        k = key_read();
        if (mode == MODE_CMDLINE) {
            int prev_mode = mode;
            cmdline_key(k);
            if (prev_mode == MODE_CMDLINE && mode == MODE_NORMAL
                && (k == 0x0d || k == 0x0a)) {
                if (ex_execute()) break;
            }
        } else if (mode == MODE_INSERT) {
            insert_key(k);
        } else if (mode == MODE_TUT) {
            tut_key(k);
        } else {
            if (normal_key(k)) break;
        }
        ensure_visible();
        draw_all();
    }

    tgoto(SCREEN_H, 1);
    tclr_eol();
    tputs("\r\n");
    return 0;
}

#endif /* !TEST_MAIN */
