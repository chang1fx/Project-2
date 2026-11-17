/* forkret.c
 *	Phase 7 test: the child's function RETURNS instead of calling
 *	Exit. Control must go back to the instruction after the initial
 *	Fork in main, so the child then runs the rest of main too.
 *	Expected: both process 1 and process 2 exit with 7.
 */

#include "syscall.h"

void
child()
{
    Yield();
    /* return -- no Exit here */
}

int
main()
{
    Fork(child);
    Yield();
    Exit(7);		/* reached by the parent AND by the child */
}
