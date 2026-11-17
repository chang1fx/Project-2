/* forkbad.c
 *	Phase 7 test: Fork with bad function addresses must return -1
 *	and must not crash Nachos.
 *	  - 2      : not word-aligned
 *	  - 100000 : outside the process's address space
 *	Expected: process 1 exits with -2 (two failed forks), and no
 *	"Process 1 Fork:" line is printed.
 */

#include "syscall.h"

int
main()
{
    int a, b;

    a = Fork((void (*)()) 2);
    b = Fork((void (*)()) 100000);
    Exit(a + b);
}
