/* forktest.c
 *	Phase 5 test: the parent forks one child; both yield, then exit.
 *	Expected: parent is process 1, child is process 2.
 */

#include "syscall.h"

void
child()
{
    Yield();
    Exit(0);
}

int
main()
{
    int pid;

    pid = Fork(child);
    Yield();
    Yield();
    Exit(pid);			/* exit status = child's pid (2) */
}
