// pcb.h
//	Process Control Block (PCB) and ProcessManager.
//
//	Every user process has one PCB. A process is one address space
//	plus the kernel thread running it, so the PCB hangs off the
//	address space:
//
//	    PCB *me = currentThread->space->pcb;
//
//	The ProcessManager hands out process ids (pids) and lets you look
//	up any process's PCB by pid. pids start at 1; the first program
//	(started with -x) gets pid 1.
//
//	One global instance, processManager, is created when the first
//	program starts (StartProcess in progtest.cc).
//
//	NOTE FOR TEAMMATES (Exit / Join / Kill / Exec):
//	    - exitStatus / exited are here for Exit and Join to use.
//	    - Add any fields you need (e.g. a Condition to wait on, a
//	      list of children) below the marked line, so we don't clash.
//	    - Freeing a process = processManager->clearPID(pid),
//	      delete the PCB, delete the AddrSpace.

#ifndef PCB_H
#define PCB_H

#if defined(CHANGED) && defined(USER_PROGRAM)

#include "copyright.h"
#include "bitmap.h"
#include "synch.h"

class Thread;

#define MAX_PROCESSES 32		// size of the process table

class PCB {
  public:
    PCB(int id, PCB *parentPCB, Thread *t)
    {
        pid = id;
        parent = parentPCB;
        thread = t;
        exitStatus = 0;
        exited = FALSE;
	exitCondition = new Condition("process exit");
    }
    ~PCB()
    {
        delete exitCondition;
    }

    int pid;			// process id
    PCB *parent;		// process that created us (NULL for the first)
    Thread *thread;		// kernel thread running this process
    int exitStatus;		// set by Exit
    bool exited;		// TRUE once the process has exited
    // ---- teammates: add your fields below this line ----
    Condition *exitCondition;
};

class ProcessManager {
  public:
    ProcessManager(int maxProcesses)
    {
        size = maxProcesses;
        pids = new BitMap(size);
        pids->Mark(0);			// pid 0 is never used
        table = new PCB*[size];
        for (int i = 0; i < size; i++)
            table[i] = NULL;
        lock = new Lock("process manager lock");
    }

    ~ProcessManager()
    {
        delete pids;
        delete [] table;
        delete lock;
    }

    // Reserve a free pid; -1 if the table is full.
    int getPID()
    {
        lock->Acquire();
        int pid = pids->Find();
        lock->Release();
        return pid;
    }

    // Release a pid (the PCB itself is deleted by the caller).
    void clearPID(int pid)
    {
        lock->Acquire();
        if (pid > 0 && pid < size) {
            pids->Clear(pid);
            table[pid] = NULL;
        }
        lock->Release();
    }

    // Record the PCB that owns a pid.
    void setPCB(int pid, PCB *pcb)
    {
        lock->Acquire();
        if (pid > 0 && pid < size)
            table[pid] = pcb;
        lock->Release();
    }

    // Look up a process by pid; NULL if there is no such process.
    PCB *getPCB(int pid)
    {
        PCB *pcb = NULL;
        lock->Acquire();
        if (pid > 0 && pid < size)
            pcb = table[pid];
        lock->Release();
        return pcb;
    }

    // Only a direct parent may Join a child, once.
    int joinProcess(PCB *parentPCB, int pid)
    {
        lock->Acquire();
        PCB *child = NULL;
        if (pid > 0 && pid < size)
            child = table[pid];

        if (parentPCB == NULL || child == NULL ||
            child == parentPCB || child->parent != parentPCB) {
            lock->Release();
            return -1;
        }

        while (!child->exited)
            child->exitCondition->Wait(lock);

        int status = child->exitStatus;
        reapLocked(child);
        lock->Release();
        return status;
    }

    // Address space must already have been released.
    void finishProcess(PCB *pcb, int status)
    {
        lock->Acquire();
        pcb->exitStatus = status;
        pcb->exited = TRUE;
        pcb->thread = NULL;

        for (int i = 1; i < size; i++) {
            PCB *child = table[i];
            if (child != NULL && child->parent == pcb) {
                child->parent = NULL;
                if (child->exited)
                    reapLocked(child);
            }
        }

        if (pcb->parent == NULL)
            reapLocked(pcb);
        else
            pcb->exitCondition->Broadcast(lock);

        lock->Release();
    }

  private:
    // Caller holds lock; the process has already exited.
    void reapLocked(PCB *pcb)
    {
        ASSERT(lock->isHeldByCurrentThread());
        ASSERT(pcb->exited);
        int pid = pcb->pid;
        table[pid] = NULL;
        pids->Clear(pid);
        delete pcb;
    }
    int size;			// number of slots
    BitMap *pids;		// set bit = pid in use
    PCB **table;		// pid -> PCB
    Lock *lock;			// makes each operation atomic
};

extern ProcessManager *processManager;

#endif // CHANGED && USER_PROGRAM

#endif // PCB_H
