/*
 * hi_stub.c -- like hi_bdos.c but provides an empty _calcSum() stub.
 * If the hang is caused by library's _calcSum(), our definition (linked
 * first) will override it and the program will reach main().
 */
#include <dos.h>

static char msg[] = "hi with stubbed calcSum\r\n$";

/* LSI-C86 appends '_' to C names. asm symbol _calcSum_ ⇒ C name _calcSum.
 * The library bundles both _calcSum and _checkSum in one .obj, so if we
 * want our stub to win we must also provide _checkSum to prevent the lib
 * module from being pulled in. */
void _calcSum(void)
{
    /* stub: do nothing */
}

int _checkSum(void)
{
    return 0;
}

int main(void)
{
    bdosp(9, msg);
    return 0;
}
