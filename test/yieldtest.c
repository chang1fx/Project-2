/* yieldtest.c
 *	Phase 1 test: call Yield three times, then Halt.
 *	Each call should print one "System Call" line, and the program
 *	must not loop on the same syscall (proves the PC is advanced).
 */

#include "syscall.h"

int
main()
{
    Yield();
    Yield();
    Yield();
    Halt();
    /* not reached */
}
