/* fork3.c
 *	Phase 7 test: the parent tries to fork three children. Every
 *	process yields back and forth, then exits.
 *
 *	Each child exits with its own number (1, 2, 3). The parent exits
 *	with the number of forks that succeeded, so the output shows how
 *	many processes fit in physical memory. A Fork that fails for lack
 *	of memory must return -1 without crashing.
 *
 *	The Fork results are stored first and counted only at the end.
 *	They are compared with -1 rather than tested with "> 0": the
 *	simulated MIPS CPU has a load delay, and this cross-compiler can
 *	emit a branch-on-zero right after the load, which then tests the
 *	register's OLD value. Comparing with a constant puts an extra
 *	instruction between the load and the branch.
 */

#include "syscall.h"

void
child1()
{
    int i;
    for (i = 0; i < 3; i++)
	Yield();
    Exit(1);
}

void
child2()
{
    int i;
    for (i = 0; i < 3; i++)
	Yield();
    Exit(2);
}

void
child3()
{
    int i;
    for (i = 0; i < 3; i++)
	Yield();
    Exit(3);
}

int
main()
{
    int i, a, b, c, ok;

    a = Fork(child1);
    b = Fork(child2);
    c = Fork(child3);

    for (i = 0; i < 5; i++)
	Yield();

    ok = 0;
    if (a != -1) ok = ok + 1;
    if (b != -1) ok = ok + 1;
    if (c != -1) ok = ok + 1;
    Exit(ok);			/* number of children created */
}
