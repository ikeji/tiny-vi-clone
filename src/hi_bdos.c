/*
 * hello_bdos.c -- diagnostic: bypass stdio entirely, use INT 21h AH=09
 * via LSI-C86's bdosp() wrapper. If printf/stdio startup is the culprit,
 * this should still work on MS-DOS 2.0.
 *
 * INT 21h AH=09: DS:DX -> $-terminated string.
 */
#include <dos.h>

static char msg[] = "hi from bdos\r\n$";

int main(void)
{
    bdosp(9, msg);
    return 0;
}
