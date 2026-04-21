/*
 * hello_crlf.c -- diagnostic: identical to hello.c but with an
 * explicit CRLF. If DOS 2.0's CON driver on this machine doesn't
 * translate lone LF to CRLF, the original hello.c may have printed
 * off-screen or overwritten the prompt.
 */
#include <stdio.h>

int main(void)
{
    printf("hi from printf\r\n");
    return 0;
}
