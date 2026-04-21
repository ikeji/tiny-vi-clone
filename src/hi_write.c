/*
 * hello_write.c -- diagnostic: bypass stdio buffering by using
 * the POSIX-ish write() syscall (INT 21h AH=40 under the hood).
 * Does not involve printf's varargs/format machinery.
 */
#include <io.h>

int main(void)
{
    write(1, "hi from write\r\n", 15);
    return 0;
}
