// memorymanager.h
//	Tracks which physical pages of main memory are free, so that
//	several user programs can be in memory at the same time, each
//	in its own pages.
//
//	One global instance, memoryManager, is created the first time an
//	address space is built (see addrspace.cc).
//
//	Interface (all operations are atomic):
//	    int  getPage()          -- allocate a free physical page;
//	                               returns its number, or -1 if
//	                               memory is full
//	    void clearPage(int i)   -- give physical page i back
//	    int  freePages()        -- how many pages are free right now

#ifndef MEMORYMANAGER_H
#define MEMORYMANAGER_H

#if defined(CHANGED) && defined(USER_PROGRAM)

#include "copyright.h"
#include "bitmap.h"
#include "synch.h"

class MemoryManager {
  public:
    MemoryManager(int numPages)
    {
        pages = new BitMap(numPages);
        lock = new Lock("memory manager lock");
    }

    ~MemoryManager()
    {
        delete pages;
        delete lock;
    }

    // Allocate a free physical page; -1 if none are left.
    int getPage()
    {
        lock->Acquire();
        int page = pages->Find();       // finds a clear bit and marks it
        lock->Release();
        return page;
    }

    // Return a physical page to the free pool.
    void clearPage(int page)
    {
        lock->Acquire();
        pages->Clear(page);
        lock->Release();
    }

    // Number of free physical pages.
    int freePages()
    {
        lock->Acquire();
        int n = pages->NumClear();
        lock->Release();
        return n;
    }

  private:
    BitMap *pages;      // one bit per physical page, set = in use
    Lock *lock;         // makes each operation atomic
};

extern MemoryManager *memoryManager;

#endif // CHANGED && USER_PROGRAM

#endif // MEMORYMANAGER_H
