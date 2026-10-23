/*
 * vitest.c -- unit tests for vi-clone buffer/editing logic.
 * Compiled with TEST_MAIN defined to skip vi.c's main() and the
 * conio/jctype dependencies.
 *
 * Output format (written to stdout): one line per test case,
 *   "PASS <name>" or "FAIL <name>: <detail>"
 * followed by a final summary "SUMMARY pass=<n> fail=<n>".
 *
 * Exit code: 0 if all pass, 1 otherwise.
 */

#define TEST_MAIN
#include "vi.c"

#include <stdio.h>
#include <string.h>

static int tests_pass = 0;
static int tests_fail = 0;

static void reset_state(void)
{
    buf_len = 0;
    cur_off = 0;
    desired_col = -1;
    top_line = 0;
    mode = MODE_NORMAL;
    pending_op = 0;
    filename[0] = '\0';
    dirty = 0;
    msg[0] = '\0';
    cmdline_len = 0;
    yank_len = 0;
    yank_linewise = 0;
    undo_valid = 0;
    reindex();
}

static void load_text(const char *s)
{
    int n = (int)strlen(s);
    if (n > BUF_MAX) n = BUF_MAX;
    memcpy(buf, s, n);
    buf_len = n;
    dirty = 0;
    cur_off = 0;
    top_line = 0;
    reindex();
}

static void check_eq_str(const char *name, const char *expected)
{
    int n = (int)strlen(expected);
    if (n == buf_len && memcmp(buf, expected, n) == 0) {
        printf("PASS %s\n", name);
        tests_pass++;
    } else {
        printf("FAIL %s: expected [%s] (len %d), got [",
               name, expected, n);
        {
            int i;
            for (i = 0; i < buf_len; i++) {
                if (buf[i] >= 0x20 && buf[i] < 0x7f) putchar(buf[i]);
                else if (buf[i] == '\n') { putchar('\\'); putchar('n'); }
                else { printf("\\x%02x", (unsigned char)buf[i]); }
            }
        }
        printf("] (len %d)\n", buf_len);
        tests_fail++;
    }
}

static void check_eq_int(const char *name, int got, int expected)
{
    if (got == expected) {
        printf("PASS %s\n", name);
        tests_pass++;
    } else {
        printf("FAIL %s: expected %d, got %d\n", name, expected, got);
        tests_fail++;
    }
}

/* ---------- tests ---------- */

static void test_insert_basic(void)
{
    reset_state();
    do_insert_byte('a');
    do_insert_byte('b');
    do_insert_byte('c');
    check_eq_str("insert_basic", "abc");
    check_eq_int("insert_basic_cursor", cur_off, 3);
}

static void test_insert_newline(void)
{
    reset_state();
    load_text("hello");
    cur_off = 5;
    do_insert_newline();
    do_insert_byte('w');
    do_insert_byte('o');
    check_eq_str("insert_newline", "hello\nwo");
}

static void test_delete_char(void)
{
    reset_state();
    load_text("hello");
    cur_off = 1;
    do_delete_char();
    check_eq_str("delete_char", "hllo");
    check_eq_int("delete_char_cursor", cur_off, 1);
}

static void test_delete_line_middle(void)
{
    reset_state();
    load_text("one\ntwo\nthree\n");
    cur_off = line_start(1); /* on "two" */
    do_delete_line();
    check_eq_str("delete_line_middle", "one\nthree\n");
}

static void test_delete_line_last_with_prior(void)
{
    reset_state();
    load_text("a\nb\nc");
    cur_off = line_start(2); /* on "c" */
    do_delete_line();
    check_eq_str("delete_line_last", "a\nb");
}

static void test_delete_line_only(void)
{
    reset_state();
    load_text("only");
    do_delete_line();
    check_eq_str("delete_line_only", "");
}

static void test_move_left_right(void)
{
    reset_state();
    load_text("abc");
    cur_off = 0;
    move_right(); check_eq_int("move_right_1", cur_off, 1);
    move_right(); check_eq_int("move_right_2", cur_off, 2);
    move_right(); /* at end of line "abc"; should stay on 'c' */
    check_eq_int("move_right_end", cur_off, 2);
    move_left(); check_eq_int("move_left_1", cur_off, 1);
    move_left(); check_eq_int("move_left_2", cur_off, 0);
    move_left(); check_eq_int("move_left_clamped", cur_off, 0);
}

static void test_move_up_down(void)
{
    reset_state();
    load_text("abc\ndefg\nh");
    cur_off = 1; /* 'b' on line 0 */
    move_down(); check_eq_int("move_down_col1", cur_off, 5);   /* 'e' */
    move_down(); check_eq_int("move_down_col1_again", cur_off, 9); /* 'h' (line is short, clamp) */
    move_up();   check_eq_int("move_up_back", cur_off, 5);
    move_up();   check_eq_int("move_up_top", cur_off, 1);
}

static void test_home_end(void)
{
    reset_state();
    load_text("hello world");
    cur_off = 5;
    move_home(); check_eq_int("home", cur_off, 0);
    move_end();  check_eq_int("end", cur_off, 10); /* on 'd' */
}

static void test_top_bottom(void)
{
    reset_state();
    load_text("one\ntwo\nthree");
    cur_off = 4;
    move_bottom(); check_eq_int("bottom", cur_off, 8); /* start of "three" */
    move_top();    check_eq_int("top", cur_off, 0);
}

static void test_sjis_insert_and_navigate(void)
{
    /* "ã‚" in SJIS = 0x82 0xA0 */
    reset_state();
    do_insert_byte((char)0x82);
    do_insert_byte((char)0xa0);
    do_insert_byte('z');
    check_eq_int("sjis_len", buf_len, 3);
    check_eq_int("sjis_cursor_end", cur_off, 3);

    cur_off = 0;
    move_right(); /* should jump over "ã‚" (2 bytes) */
    check_eq_int("sjis_move_right_2bytes", cur_off, 2);
    move_left();  /* back to start of "ã‚" */
    check_eq_int("sjis_move_left_2bytes", cur_off, 0);
}

static void test_sjis_delete_char(void)
{
    /* "ã‚ã„" in SJIS = 82 A0 82 A2 */
    reset_state();
    do_insert_byte((char)0x82);
    do_insert_byte((char)0xa0);
    do_insert_byte((char)0x82);
    do_insert_byte((char)0xa2);
    cur_off = 0;
    do_delete_char();
    check_eq_int("sjis_delete_len", buf_len, 2);
    /* remaining should be "ã„" (82 A2) */
    check_eq_int("sjis_delete_b0", (unsigned char)buf[0], 0x82);
    check_eq_int("sjis_delete_b1", (unsigned char)buf[1], 0xa2);
}

static void test_insert_backspace(void)
{
    reset_state();
    load_text("hello");
    cur_off = 3;
    do_insert_backspace();
    check_eq_str("insert_bs", "helo");
    check_eq_int("insert_bs_cursor", cur_off, 2);
}

static void test_insert_backspace_sjis(void)
{
    reset_state();
    do_insert_byte((char)0x82);
    do_insert_byte((char)0xa0);
    do_insert_byte('x');
    /* cur_off = 3 */
    do_insert_backspace();  /* remove 'x' */
    check_eq_int("bs_sjis_cursor", cur_off, 2);
    do_insert_backspace();  /* should remove both SJIS bytes */
    check_eq_int("bs_sjis_both", cur_off, 0);
    check_eq_int("bs_sjis_len", buf_len, 0);
}

static void test_reindex_and_line_of(void)
{
    reset_state();
    load_text("a\nbb\nccc\n");
    check_eq_int("line_count", line_count, 4); /* 4 starts: 0, 2, 5, 9 */
    check_eq_int("line_start_0", line_start(0), 0);
    check_eq_int("line_start_1", line_start(1), 2);
    check_eq_int("line_start_2", line_start(2), 5);
    check_eq_int("line_of_0", line_of(0), 0);
    check_eq_int("line_of_3", line_of(3), 1);
    check_eq_int("line_of_6", line_of(6), 2);
}

static void test_o_and_O_simulation(void)
{
    /* Simulate 'o' at line 0 of "abc\ndef" */
    reset_state();
    load_text("abc\ndef");
    cur_off = 0;
    move_end();  /* cur_off = 2 (on 'c') */
    if (cur_off < line_end(line_of(cur_off))) cur_off = next_byte(cur_off); /* 3 */
    do_insert_newline();   /* insert '\n' at pos 3; buf = "abc\n\ndef" */
    /* cur_off is now 4, on blank line */
    do_insert_byte('X');
    check_eq_str("o_simulation", "abc\nX\ndef");
}

static void test_file_save_load_roundtrip(void)
{
    int rc;
    reset_state();
    load_text("hello\nworld\n");
    strcpy(filename, "TEST.TXT");
    rc = file_save("TEST.TXT");
    check_eq_int("save_rc", rc, 0);

    reset_state();
    rc = file_load("TEST.TXT");
    check_eq_int("load_rc", rc, 0);
    check_eq_str("load_content", "hello\nworld\n");
}

static void test_ex_write_quit(void)
{
    int rc;
    reset_state();
    load_text("abc");
    strcpy(filename, "EX1.TXT");
    strcpy(cmdline, "w");
    cmdline_len = 1;
    rc = ex_execute();
    check_eq_int("ex_w_no_quit", rc, 0);

    reset_state();
    rc = file_load("EX1.TXT");
    check_eq_int("ex_w_load_ok", rc, 0);
    check_eq_str("ex_w_content", "abc");

    /* :q with dirty should refuse */
    dirty = 1;
    strcpy(cmdline, "q");
    cmdline_len = 1;
    rc = ex_execute();
    check_eq_int("ex_q_dirty_refuses", rc, 0);

    /* :q! should quit */
    strcpy(cmdline, "q!");
    cmdline_len = 2;
    rc = ex_execute();
    check_eq_int("ex_q_bang_quits", rc, 1);
}

static void test_zz_command(void)
{
    int rc;
    reset_state();
    load_text("hello world");
    strcpy(filename, "ZZTEST.TXT");
    dirty = 1;

    rc = normal_key('Z');
    check_eq_int("zz_first_Z_no_quit", rc, 0);
    check_eq_int("zz_pending", pending_op, 'Z');
    rc = normal_key('Z');
    check_eq_int("zz_second_Z_quits", rc, 1);

    reset_state();
    rc = file_load("ZZTEST.TXT");
    check_eq_int("zz_save_ok", rc, 0);
    check_eq_str("zz_saved_content", "hello world");
}

static void test_tut_basic(void)
{
    reset_state();
    mode = MODE_TUT;
    tut_reset();
    tut_key('r');
    check_eq_int("tut_r_waits", buf_len, 0);  /* no commit yet */
    tut_key('k');
    check_eq_int("tut_rk_len", buf_len, 2);
    check_eq_int("tut_rk_b0", (unsigned char)buf[0], 0x82);
    check_eq_int("tut_rk_b1", (unsigned char)buf[1], 0xa0);  /* ã‚ */
    check_eq_int("tut_stays_in_mode", mode, MODE_TUT);
    check_eq_int("tut_buf_cleared", tut_len, 0);

    /* multiple kana in a row */
    tut_key('s');
    tut_key('i');  /* ã— */
    check_eq_int("tut_shi_len", buf_len, 4);
    check_eq_int("tut_shi_b2", (unsigned char)buf[2], 0x82);
    check_eq_int("tut_shi_b3", (unsigned char)buf[3], 0xb5);
}

static void test_tut_prefix_wait(void)
{
    /* 'd' is a prefix (dk, di, du, dh, dj, dl*); don't commit */
    reset_state();
    mode = MODE_TUT;
    tut_reset();
    tut_key('d');
    check_eq_int("tut_d_waits_len", buf_len, 0);
    check_eq_int("tut_d_in_buf", tut_buf[0], 'd');
    check_eq_int("tut_d_len", tut_len, 1);
    tut_key('l');   /* dl still a prefix for dakuten */
    check_eq_int("tut_dl_waits", buf_len, 0);
    check_eq_int("tut_dl_len", tut_len, 2);
    tut_key('u');   /* dlu = ã£ */
    check_eq_int("tut_dlu_commit", buf_len, 2);
    check_eq_int("tut_dlu_b0", (unsigned char)buf[0], 0x82);
    check_eq_int("tut_dlu_b1", (unsigned char)buf[1], 0xc1);
}

static void test_tut_dead_sequence(void)
{
    /* 'a' is a prefix ('ab...' entries); 'ab' is also only a prefix
     * (no exact match); 'aba' matches nothing and is not a prefix,
     * so the whole sequence is abandoned silently. */
    reset_state();
    mode = MODE_TUT;
    tut_reset();
    tut_key('a');
    tut_key('b');
    check_eq_int("tut_dead_still_waiting", buf_len, 0);
    check_eq_int("tut_dead_prefix_len", tut_len, 2);
    tut_key('a');
    check_eq_int("tut_dead_no_commit", buf_len, 0);
    check_eq_int("tut_dead_buf_reset", tut_len, 0);
}

static void test_tut_dakuten(void)
{
    /* elk = ãŒ (0x82aa), tltk = ã± (0x82cf), dldu = ã¥ (0x82c3) */
    reset_state();
    mode = MODE_TUT;
    tut_reset();
    tut_key('e'); tut_key('l'); tut_key('k');
    check_eq_int("tut_ga_b1", (unsigned char)buf[1], 0xaa);
    tut_key('t'); tut_key('l'); tut_key('t'); tut_key('k');
    check_eq_int("tut_pa_b1", (unsigned char)buf[3], 0xcf);
    tut_key('d'); tut_key('l'); tut_key('d'); tut_key('u');
    check_eq_int("tut_du_b1", (unsigned char)buf[5], 0xc3);
}

static void test_tut_backspace(void)
{
    /* Case 1: BS while mid-stroke -- cancels last keystroke only. */
    reset_state();
    mode = MODE_TUT;
    tut_reset();
    tut_key('r');
    check_eq_int("tut_bs_before_len", tut_len, 1);
    tut_key(0x08);
    check_eq_int("tut_bs_after_len", tut_len, 0);
    check_eq_int("tut_bs_no_commit", buf_len, 0);

    /* Case 2: BS with no pending stroke -- deletes last char (ASCII). */
    reset_state();
    load_text("abc");
    cur_off = buf_len;
    mode = MODE_TUT;
    tut_reset();
    tut_key(0x08);
    check_eq_int("tut_bs_deletes_ascii_len", buf_len, 2);
    check_eq_str("tut_bs_deletes_ascii", "ab");
    check_eq_int("tut_bs_cursor", cur_off, 2);

    /* Case 3: BS with no pending stroke deletes a 2-byte SJIS char whole. */
    reset_state();
    mode = MODE_TUT;
    tut_reset();
    tut_key('r'); tut_key('k');   /* commits ã‚ (82 A0) */
    check_eq_int("tut_bs_sjis_committed", buf_len, 2);
    tut_key(0x08);                /* BS should remove both bytes */
    check_eq_int("tut_bs_sjis_deleted_len", buf_len, 0);
    check_eq_int("tut_bs_sjis_cursor", cur_off, 0);
}

static void test_tut_newline(void)
{
    /* CR in TUT mode inserts a newline and stays in TUT mode. */
    reset_state();
    mode = MODE_TUT;
    tut_reset();
    tut_key('r'); tut_key('k');   /* ã‚ */
    tut_key(0x0d);                /* newline */
    tut_key('r'); tut_key('k');   /* ã‚ */
    check_eq_int("tut_cr_len", buf_len, 5);           /* 2 + 1 + 2 */
    check_eq_int("tut_cr_mid", buf[2], '\n');
    check_eq_int("tut_cr_still_tut", mode, MODE_TUT);

    /* CR mid-stroke discards the pending stroke and inserts newline. */
    reset_state();
    mode = MODE_TUT;
    tut_reset();
    tut_key('r');                 /* pending */
    check_eq_int("tut_cr_pending_before", tut_len, 1);
    tut_key(0x0d);
    check_eq_int("tut_cr_pending_cleared", tut_len, 0);
    check_eq_int("tut_cr_pending_len", buf_len, 1);
    check_eq_int("tut_cr_pending_byte", buf[0], '\n');
}

static void test_tut_esc_exits(void)
{
    reset_state();
    mode = MODE_TUT;
    tut_reset();
    tut_key('r');
    tut_key(0x1b);
    check_eq_int("tut_esc_back_to_insert", mode, MODE_INSERT);
    check_eq_int("tut_esc_buf_cleared", tut_len, 0);
    check_eq_int("tut_esc_no_commit", buf_len, 0);
}

static void test_tut_kanji(void)
{
    /* "rz" = ç†± (SJIS 0x944d). Confirms the generated kanji table works. */
    reset_state();
    mode = MODE_TUT;
    tut_reset();
    tut_key('r');
    tut_key('z');
    check_eq_int("tut_kanji_len", buf_len, 2);
    check_eq_int("tut_kanji_b0", (unsigned char)buf[0], 0x94);
    check_eq_int("tut_kanji_b1", (unsigned char)buf[1], 0x4d);

    /* ",," = å€‹ (SJIS 0x8cc2) -- tests punctuation-keyed kanji. */
    reset_state();
    mode = MODE_TUT;
    tut_reset();
    tut_key(',');
    tut_key(',');
    check_eq_int("tut_punct_kanji_len", buf_len, 2);
    check_eq_int("tut_punct_kanji_b0", (unsigned char)buf[0], 0x8c);
    check_eq_int("tut_punct_kanji_b1", (unsigned char)buf[1], 0xc2);
}

static void test_screen_size_env(void)
{
    /* Exercise init_screen_size with various env configs. */
    extern int SCREEN_W, SCREEN_H, TEXT_H;
    int saved_w = SCREEN_W, saved_h = SCREEN_H, saved_t = TEXT_H;

    /* Defaults when nothing is set. LSI-C86 has putenv. */
    putenv("VI_COLS=");
    putenv("VI_ROWS=");
    putenv("COLUMNS=");
    putenv("LINES=");
    SCREEN_W = 80; SCREEN_H = 25; TEXT_H = 23;
    init_screen_size();
    check_eq_int("screen_default_w", SCREEN_W, 80);
    check_eq_int("screen_default_h", SCREEN_H, 25);
    check_eq_int("screen_default_t", TEXT_H, 23);

    /* COLUMNS/LINES (POSIX). */
    putenv("COLUMNS=100");
    putenv("LINES=40");
    SCREEN_W = 80; SCREEN_H = 25;
    init_screen_size();
    check_eq_int("screen_posix_w", SCREEN_W, 100);
    check_eq_int("screen_posix_h", SCREEN_H, 40);
    check_eq_int("screen_posix_t", TEXT_H, 38);

    /* VI_* overrides POSIX. */
    putenv("VI_COLS=120");
    putenv("VI_ROWS=50");
    SCREEN_W = 80; SCREEN_H = 25;
    init_screen_size();
    check_eq_int("screen_vi_w", SCREEN_W, 120);
    check_eq_int("screen_vi_h", SCREEN_H, 50);

    /* Out-of-range values are ignored. */
    putenv("VI_COLS=10");   /* below minimum 20 */
    putenv("VI_ROWS=999");  /* above maximum 200 */
    putenv("COLUMNS=");
    putenv("LINES=");
    SCREEN_W = 80; SCREEN_H = 25;
    init_screen_size();
    check_eq_int("screen_clamp_w", SCREEN_W, 80);
    check_eq_int("screen_clamp_h", SCREEN_H, 25);

    SCREEN_W = saved_w; SCREEN_H = saved_h; TEXT_H = saved_t;
}

static void test_ctrl_j_enters_tut(void)
{
    reset_state();
    mode = MODE_INSERT;
    insert_key(0x0a);  /* Ctrl+J */
    check_eq_int("ctrl_j_mode", mode, MODE_TUT);
    check_eq_int("ctrl_j_no_newline", buf_len, 0);
    /* CR still inserts newline in insert mode */
    reset_state();
    mode = MODE_INSERT;
    insert_key(0x0d);
    check_eq_int("cr_inserts_newline", buf_len, 1);
    check_eq_int("cr_is_lf", buf[0], '\n');
}

/* ---------- yank / put ---------- */

static void test_yank_put_line(void)
{
    reset_state();
    load_text("one\ntwo\nthree");
    cur_off = line_start(1);          /* on "two" */
    normal_key('y'); normal_key('y');
    check_eq_int("yy_len", yank_len, 3);
    check_eq_int("yy_linewise", yank_linewise, 1);
    check_eq_str("yy_no_change", "one\ntwo\nthree");
    normal_key('p');                  /* below "two" */
    check_eq_str("yy_p_below", "one\ntwo\ntwo\nthree");
    check_eq_int("yy_p_cursor", cur_off, line_start(2));
    normal_key('P');                  /* above the new line */
    check_eq_str("yy_P_above", "one\ntwo\ntwo\ntwo\nthree");
    check_eq_int("yy_P_cursor", cur_off, line_start(2));
}

static void test_put_line_at_end(void)
{
    /* p on the last line (no trailing '\n') appends "\n" + text */
    reset_state();
    load_text("a\nb");
    cur_off = 0;
    normal_key('y'); normal_key('y');
    move_bottom();
    normal_key('p');
    check_eq_str("p_last_line", "a\nb\na");
    check_eq_int("p_last_line_cursor", cur_off, 4);

    /* p on the (empty) line after a trailing '\n' */
    reset_state();
    load_text("a\nb\n");
    cur_off = 0;
    normal_key('y'); normal_key('y');
    move_bottom();
    normal_key('p');
    check_eq_str("p_after_trailing_nl", "a\nb\n\na");
}

static void test_dd_then_put(void)
{
    reset_state();
    load_text("one\ntwo\nthree");
    cur_off = line_start(0);
    normal_key('d'); normal_key('d');
    check_eq_str("dd_removed", "two\nthree");
    check_eq_int("dd_yank_len", yank_len, 3);
    check_eq_int("dd_yank_linewise", yank_linewise, 1);
    move_bottom();
    normal_key('p');
    check_eq_str("dd_p_moves_line", "two\nthree\none");

    /* deleting the last line keeps its text without the leading '\n' */
    reset_state();
    load_text("a\nbb");
    move_bottom();
    normal_key('d'); normal_key('d');
    check_eq_str("dd_last_removed", "a");
    check_eq_int("dd_last_yank_len", yank_len, 2);
    check_eq_int("dd_last_yank_b0", yank_buf[0], 'b');
    normal_key('P');
    check_eq_str("dd_last_P", "bb\na");
}

static void test_x_then_put(void)
{
    reset_state();
    load_text("abc\n");
    cur_off = 0;
    normal_key('x');                  /* delete 'a' */
    check_eq_str("x_removed", "bc\n");
    check_eq_int("x_yank_charwise", yank_linewise, 0);
    check_eq_int("x_yank_len", yank_len, 1);
    normal_key('p');                  /* after 'b' */
    check_eq_str("x_p_after", "bac\n");
    check_eq_int("x_p_cursor", cur_off, 1);
    move_end();
    normal_key('P');                  /* at cursor ('c') */
    check_eq_str("x_P_before", "baac\n");
    check_eq_int("x_P_cursor", cur_off, 2);
}

static void test_x_put_sjis(void)
{
    /* "‚ ‚¢" = 82 A0 82 A2; x yanks 2 bytes, p puts them whole */
    reset_state();
    do_insert_byte((char)0x82); do_insert_byte((char)0xa0);
    do_insert_byte((char)0x82); do_insert_byte((char)0xa2);
    mode = MODE_NORMAL;
    cur_off = 0;
    normal_key('x');
    check_eq_int("x_sjis_yank_len", yank_len, 2);
    check_eq_int("x_sjis_buf_len", buf_len, 2);
    normal_key('p');                  /* after ‚¢ -> ‚¢‚  */
    check_eq_int("p_sjis_len", buf_len, 4);
    check_eq_int("p_sjis_b2", (unsigned char)buf[2], 0x82);
    check_eq_int("p_sjis_b3", (unsigned char)buf[3], 0xa0);
    check_eq_int("p_sjis_cursor", cur_off, 2);  /* on the pasted char */
}

static void test_put_empty_register(void)
{
    reset_state();
    load_text("abc");
    normal_key('p');
    check_eq_str("p_empty_noop", "abc");
}

static void test_put_buffer_full(void)
{
    int i;
    reset_state();
    for (i = 0; i < BUF_MAX - 2; i++) buf[i] = 'a';
    buf_len = BUF_MAX - 2;
    reindex();
    cur_off = 0;
    normal_key('y'); normal_key('y');  /* too long to yank */
    check_eq_int("yy_too_long", yank_len, 0);
    normal_key('x');                   /* 1 byte register */
    normal_key('p');
    check_eq_int("p_fits", buf_len, BUF_MAX - 2);
    normal_key('p');
    check_eq_int("p_fits_2", buf_len, BUF_MAX - 1);
    normal_key('p');
    check_eq_int("p_fits_3", buf_len, BUF_MAX);
    normal_key('p');                   /* no room */
    check_eq_int("p_full_rejected", buf_len, BUF_MAX);
}

/* ---------- undo ---------- */

static void test_undo_nothing(void)
{
    reset_state();
    load_text("abc");
    normal_key('u');
    check_eq_str("u_nothing", "abc");
    check_eq_int("u_nothing_msg", strcmp(msg, "nothing to undo"), 0);
}

static void test_undo_x_and_redo(void)
{
    reset_state();
    load_text("abc\ndef");
    cur_off = 5;                      /* 'e' */
    normal_key('x');
    check_eq_str("u_x_deleted", "abc\ndf");
    normal_key('u');
    check_eq_str("u_x_restored", "abc\ndef");
    check_eq_int("u_x_cursor", cur_off, 5);
    normal_key('u');                  /* redo */
    check_eq_str("u_x_redo", "abc\ndf");
    normal_key('u');
    check_eq_str("u_x_undo_again", "abc\ndef");
}

static void test_undo_dd(void)
{
    reset_state();
    load_text("one\ntwo\nthree");
    cur_off = line_start(2);
    normal_key('d'); normal_key('d');
    check_eq_str("u_dd_deleted", "one\ntwo");
    check_eq_int("u_dd_cursor_moved", cur_off, 4);
    normal_key('u');
    check_eq_str("u_dd_restored", "one\ntwo\nthree");
    check_eq_int("u_dd_cursor_back", cur_off, 8);
}

static void test_undo_put(void)
{
    reset_state();
    load_text("one\ntwo");
    normal_key('y'); normal_key('y');
    normal_key('p');
    check_eq_str("u_p_put", "one\none\ntwo");
    normal_key('u');
    check_eq_str("u_p_undone", "one\ntwo");
    check_eq_int("u_p_cursor", cur_off, 0);
}

static void test_undo_insert_session(void)
{
    /* The whole i...ESC session is one undo unit. */
    reset_state();
    load_text("hello");
    cur_off = 4;                      /* 'o' */
    normal_key('A');
    insert_key(' ');
    insert_key('w');
    insert_key(0x0d);
    insert_key('x');
    insert_key(0x08);
    insert_key('y');
    insert_key(0x1b);
    check_eq_str("u_ins_result", "hello w\ny");
    normal_key('u');
    check_eq_str("u_ins_undone", "hello");
    check_eq_int("u_ins_cursor", cur_off, 4);
    normal_key('u');
    check_eq_str("u_ins_redone", "hello w\ny");

    /* TUT input inside the session is part of the same unit. */
    reset_state();
    load_text("ab");
    cur_off = 0;
    normal_key('i');
    insert_key(0x0a);                 /* Ctrl+J -> TUT */
    tut_key('r'); tut_key('k');       /* ‚  */
    tut_key(0x1b);
    insert_key(0x1b);
    check_eq_int("u_tut_len", buf_len, 4);
    normal_key('u');
    check_eq_str("u_tut_undone", "ab");
}

static void test_undo_o_and_O(void)
{
    reset_state();
    load_text("a\nb");
    cur_off = 0;
    normal_key('o');
    insert_key('z');
    insert_key(0x1b);
    check_eq_str("u_o_result", "a\nz\nb");
    normal_key('u');
    check_eq_str("u_o_undone", "a\nb");

    move_bottom();
    normal_key('O');
    insert_key(0x1b);                 /* empty line only */
    check_eq_str("u_O_result", "a\n\nb");
    normal_key('u');
    check_eq_str("u_O_undone", "a\nb");
    check_eq_int("u_O_cursor", cur_off, 2);
}

static void test_undo_noop_change(void)
{
    /* i then ESC without typing: 'u' reports nothing to undo */
    reset_state();
    load_text("abc");
    cur_off = 1;
    normal_key('i');
    insert_key(0x1b);
    normal_key('u');
    check_eq_str("u_noop_unchanged", "abc");
    check_eq_int("u_noop_msg", strcmp(msg, "nothing to undo"), 0);
}

static void test_undo_shrinks_to_empty(void)
{
    reset_state();
    load_text("only");
    normal_key('d'); normal_key('d');
    check_eq_str("u_empty_deleted", "");
    normal_key('u');
    check_eq_str("u_empty_restored", "only");
    normal_key('u');
    check_eq_str("u_empty_redo", "");
    check_eq_int("u_empty_cursor", cur_off, 0);
}

int main(void)
{
    test_insert_basic();
    test_insert_newline();
    test_delete_char();
    test_delete_line_middle();
    test_delete_line_last_with_prior();
    test_delete_line_only();
    test_move_left_right();
    test_move_up_down();
    test_home_end();
    test_top_bottom();
    test_sjis_insert_and_navigate();
    test_sjis_delete_char();
    test_insert_backspace();
    test_insert_backspace_sjis();
    test_reindex_and_line_of();
    test_o_and_O_simulation();
    test_file_save_load_roundtrip();
    test_ex_write_quit();
    test_zz_command();
    test_tut_basic();
    test_tut_prefix_wait();
    test_tut_dead_sequence();
    test_tut_dakuten();
    test_tut_backspace();
    test_tut_newline();
    test_tut_esc_exits();
    test_tut_kanji();
    test_screen_size_env();
    test_ctrl_j_enters_tut();
    test_yank_put_line();
    test_put_line_at_end();
    test_dd_then_put();
    test_x_then_put();
    test_x_put_sjis();
    test_put_empty_register();
    test_put_buffer_full();
    test_undo_nothing();
    test_undo_x_and_redo();
    test_undo_dd();
    test_undo_put();
    test_undo_insert_session();
    test_undo_o_and_O();
    test_undo_noop_change();
    test_undo_shrinks_to_empty();

    printf("SUMMARY pass=%d fail=%d\n", tests_pass, tests_fail);
    return tests_fail == 0 ? 0 : 1;
}
