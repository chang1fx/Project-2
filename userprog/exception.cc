// exception.cc
//	Entry point into the Nachos kernel from user programs.
//	There are two kinds of things that can cause control to
//	transfer back to here from user code:
//
//	syscall -- The user code explicitly requests to call a procedure
//	in the Nachos kernel.  Right now, the only function we support is
//	"Halt".
//
//	exceptions -- The user code does something that the CPU can't handle.
//	For instance, accessing memory that doesn't exist, arithmetic errors,
//	etc.
//
//	Interrupts (which can also cause control to transfer from user
//	code into the Nachos kernel) are handled elsewhere.
//
// For now, this only handles the Halt() system call.
// Everything else core dumps.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "syscall.h"

#if defined(CHANGED) && defined(USER_PROGRAM)
#include "pcb.h"

//----------------------------------------------------------------------
// IncrementPC
//	Advance the user program counter past the syscall instruction.
//	Without this, returning to user mode re-executes the same
//	syscall forever.
//----------------------------------------------------------------------

static void
IncrementPC()
{
    int pc = machine->ReadRegister(PCReg);
    machine->WriteRegister(PrevPCReg, pc);
    pc = machine->ReadRegister(NextPCReg);
    machine->WriteRegister(PCReg, pc);
    machine->WriteRegister(NextPCReg, pc + 4);
}

//----------------------------------------------------------------------
// CurrentPid
//	Process id of the running user program (-1 if it has no PCB).
//----------------------------------------------------------------------

static int
CurrentPid()
{
    if (currentThread->space == NULL || currentThread->space->pcb == NULL)
	return -1;
    return currentThread->space->pcb->pid;
}

//----------------------------------------------------------------------
// Fork support
//	The parent's user registers are captured at the moment of the
//	syscall and handed to the new kernel thread, which loads them,
//	points the PC at "func", and starts running user code.
//
//	All registers are copied unchanged except the PC. In particular
//	the return-address register (r31) still holds the address just
//	after the parent's call to Fork(), so when "func" returns, the
//	child continues at the instruction after the initial Fork.
//----------------------------------------------------------------------

struct ForkArgs {
    int registers[NumTotalRegs];	// parent's user registers
    int func;				// where the child starts
};

static void
ForkedProcessStart(int arg)
{
    ForkArgs *args = (ForkArgs *) arg;

    for (int i = 0; i < NumTotalRegs; i++)
	machine->WriteRegister(i, args->registers[i]);
    machine->WriteRegister(PrevPCReg, args->func - 4);
    machine->WriteRegister(PCReg, args->func);
    machine->WriteRegister(NextPCReg, args->func + 4);
    delete args;

    currentThread->space->RestoreState();	// load the child's page table
    machine->Run();				// jump to user code
    ASSERT(FALSE);				// Run never returns
}

static int
DoFork(int func)
{
    AddrSpace *parentSpace = currentThread->space;
    int dummy;

    // func must be a word-aligned address inside our address space
    if ((func % 4) != 0 || !parentSpace->Translate(func, &dummy))
	return -1;

    int pid = processManager->getPID();
    if (pid < 0)				// process table full
	return -1;

    AddrSpace *childSpace = new AddrSpace(parentSpace);
    if (!childSpace->IsValid()) {		// not enough memory
	delete childSpace;
	processManager->clearPID(pid);
	return -1;
    }

    Thread *childThread = new Thread("forked process");
    childThread->space = childSpace;

    PCB *childPCB = new PCB(pid, parentSpace->pcb, childThread);
    processManager->setPCB(pid, childPCB);
    childSpace->pcb = childPCB;

    ForkArgs *args = new ForkArgs;
    for (int i = 0; i < NumTotalRegs; i++)
	args->registers[i] = machine->ReadRegister(i);
    args->func = func;

    printf("Process %d Fork: start at address 0x%x with %d pages memory\n",
	   CurrentPid(), func, childSpace->GetNumPages());

    childThread->Fork(ForkedProcessStart, (int) args);
    return pid;
}

//----------------------------------------------------------------------
// DoExit
//	TEMPORARY minimal Exit so forked processes can finish while the
//	real Exit (owned by a teammate) is being written. Frees the
//	process's pid, PCB and memory, then ends its kernel thread.
//	REPLACE with the real Exit implementation.
//----------------------------------------------------------------------

static void
DoExit(int status)
{
    AddrSpace *space = currentThread->space;
    PCB *pcb = space->pcb;

    printf("Process %d exits with %d\n", pcb->pid, status);
    pcb->exitStatus = status;
    pcb->exited = TRUE;

    processManager->clearPID(pcb->pid);
    delete pcb;
    currentThread->space = NULL;	// don't save state for a dead space
    delete space;			// gives its pages back
    currentThread->Finish();
}

//----------------------------------------------------------------------
// ExceptionHandler
// 	Entry point into the Nachos kernel.  Called when a user program
//	is executing, and either does a syscall, or generates an addressing
//	or arithmetic exception.
//
// 	For system calls, the following is the calling convention:
//
// 	system call code -- r2
//		arg1 -- r4
//		arg2 -- r5
//		arg3 -- r6
//		arg4 -- r7
//
//	The result of the system call, if any, must be put back into r2.
//----------------------------------------------------------------------

void
ExceptionHandler(ExceptionType which)
{
    int type = machine->ReadRegister(2);

    if (which != SyscallException) {
	printf("Unexpected user mode exception %d %d\n", which, type);
	ASSERT(FALSE);
    }

    switch (type) {
    case SC_Halt:
	DEBUG('a', "Shutdown, initiated by user program.\n");
	interrupt->Halt();
	break;

    case SC_Fork:
	printf("System Call: %d invoked Fork\n", CurrentPid());
	machine->WriteRegister(2, DoFork(machine->ReadRegister(4)));
	IncrementPC();
	break;

    case SC_Exit:			// TEMPORARY, see DoExit
	printf("System Call: %d invoked Exit\n", CurrentPid());
	DoExit(machine->ReadRegister(4));
	break;

    case SC_Yield:
	printf("System Call: %d invoked Yield\n", CurrentPid());
	IncrementPC();			// advance first, so we resume after
					// the syscall when rescheduled
	currentThread->Yield();
	break;

    default:
	printf("Unexpected user mode exception %d %d\n", which, type);
	ASSERT(FALSE);
    }
}

#else

//----------------------------------------------------------------------
// ExceptionHandler
// 	Entry point into the Nachos kernel.  Called when a user program
//	is executing, and either does a syscall, or generates an addressing
//	or arithmetic exception.
//
// 	For system calls, the following is the calling convention:
//
// 	system call code -- r2
//		arg1 -- r4
//		arg2 -- r5
//		arg3 -- r6
//		arg4 -- r7
//
//	The result of the system call, if any, must be put back into r2.
//
// And don't forget to increment the pc before returning. (Or else you'll
// loop making the same system call forever!
//
//	"which" is the kind of exception.  The list of possible exceptions
//	are in machine.h.
//----------------------------------------------------------------------

void
ExceptionHandler(ExceptionType which)
{
    int type = machine->ReadRegister(2);

    if ((which == SyscallException) && (type == SC_Halt)) {
	DEBUG('a', "Shutdown, initiated by user program.\n");
   	interrupt->Halt();
    } else {
	printf("Unexpected user mode exception %d %d\n", which, type);
	ASSERT(FALSE);
    }
}

#endif
