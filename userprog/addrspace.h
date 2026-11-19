// addrspace.h
//	Data structures to keep track of executing user programs
//	(address spaces).
//
//	For now, we don't keep any information about address spaces.
//	The user level CPU state is saved and restored in the thread
//	executing the user program (see thread.h).
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#ifndef ADDRSPACE_H
#define ADDRSPACE_H

#include "copyright.h"
#include "filesys.h"

#define UserStackSize		1024 	// increase this as necessary!

#if defined(CHANGED) && defined(USER_PROGRAM)
class PCB;
#endif

class AddrSpace {
  public:
    AddrSpace(OpenFile *executable);	// Create an address space,
					// initializing it with the program
					// stored in the file "executable"
    ~AddrSpace();			// De-allocate an address space
#if defined(CHANGED) && defined(USER_PROGRAM)
    AddrSpace(AddrSpace *parent);	// Fork: a copy of "parent", in its
					// own newly allocated physical pages
#endif

    void InitRegisters();		// Initialize user-level CPU registers,
					// before jumping to user code

    void SaveState();			// Save/restore address space-specific
    void RestoreState();		// info on a context switch

#if defined(CHANGED) && defined(USER_PROGRAM)
    bool IsValid() { return valid; }	// FALSE if the program could not
					// be loaded (e.g. not enough memory)
    unsigned int GetNumPages() { return numPages; }

    bool Translate(int virtAddr, int *physAddr);
					// Virtual -> physical address using
					// this space's page table; FALSE if
					// the address is not mapped
    bool ReadFile(int virtAddr, OpenFile *file, int size, int fileAddr);
					// Copy "size" bytes at "fileAddr" in
					// "file" into this space, starting at
					// virtual address "virtAddr"
    
    bool ReplaceFrom(OpenFile* executable);

    PCB *pcb;				// the process that owns this space
#endif

  private:
    TranslationEntry *pageTable;	// Assume linear page table translation
					// for now!
    unsigned int numPages;		// Number of pages in the virtual
					// address space
#if defined(CHANGED) && defined(USER_PROGRAM)
    bool valid;				// TRUE once loaded successfully
#endif
};

#endif // ADDRSPACE_H
