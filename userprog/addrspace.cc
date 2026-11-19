// addrspace.cc
//	Routines to manage address spaces (executing user programs).
//
//	In order to run a user program, you must:
//
//	1. link with the -N -T 0 option
//	2. run coff2noff to convert the object file to Nachos format
//		(Nachos object code format is essentially just a simpler
//		version of the UNIX executable object code format)
//	3. load the NOFF file into the Nachos file system
//		(if you haven't implemented the file system yet, you
//		don't need to do this last step)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "addrspace.h"
#include "noff.h"
#ifdef HOST_SPARC
#include <strings.h>
#endif
#if defined(CHANGED) && defined(USER_PROGRAM)
#include <string.h>
#include "memorymanager.h"
#include "pcb.h"

MemoryManager *memoryManager = NULL;	// created by the first AddrSpace
ProcessManager *processManager = NULL;	// created in StartProcess
#endif

//----------------------------------------------------------------------
// SwapHeader
// 	Do little endian to big endian conversion on the bytes in the
//	object file header, in case the file was generated on a little
//	endian machine, and we're now running on a big endian machine.
//----------------------------------------------------------------------

static void
SwapHeader (NoffHeader *noffH)
{
	noffH->noffMagic = WordToHost(noffH->noffMagic);
	noffH->code.size = WordToHost(noffH->code.size);
	noffH->code.virtualAddr = WordToHost(noffH->code.virtualAddr);
	noffH->code.inFileAddr = WordToHost(noffH->code.inFileAddr);
	noffH->initData.size = WordToHost(noffH->initData.size);
	noffH->initData.virtualAddr = WordToHost(noffH->initData.virtualAddr);
	noffH->initData.inFileAddr = WordToHost(noffH->initData.inFileAddr);
	noffH->uninitData.size = WordToHost(noffH->uninitData.size);
	noffH->uninitData.virtualAddr = WordToHost(noffH->uninitData.virtualAddr);
	noffH->uninitData.inFileAddr = WordToHost(noffH->uninitData.inFileAddr);
}

#if defined(CHANGED) && defined(USER_PROGRAM)

//----------------------------------------------------------------------
// AddrSpace::AddrSpace
// 	Create an address space to run a user program.
//	Each virtual page gets its own free physical page from the
//	MemoryManager, so several programs can be in memory at once.
//	The code and data segments are then copied in through the page
//	table (ReadFile/Translate).
//
//	If there is not enough free memory, nothing is allocated and
//	IsValid() returns FALSE; the caller must check it.
//
//	"executable" is the file containing the object code to load into memory
//----------------------------------------------------------------------

AddrSpace::AddrSpace(OpenFile *executable)
{
    NoffHeader noffH;
    unsigned int i, size;

    valid = FALSE;
    pageTable = NULL;
    numPages = 0;
    pcb = NULL;

    if (memoryManager == NULL)
	memoryManager = new MemoryManager(NumPhysPages);

    executable->ReadAt((char *)&noffH, sizeof(noffH), 0);
    if ((noffH.noffMagic != NOFFMAGIC) &&
		(WordToHost(noffH.noffMagic) == NOFFMAGIC))
    	SwapHeader(&noffH);
    if (noffH.noffMagic != NOFFMAGIC) {
	DEBUG('a', "Not a NOFF executable\n");
	return;
    }

// how big is address space?
    size = noffH.code.size + noffH.initData.size + noffH.uninitData.size
			+ UserStackSize;	// we need to increase the size
						// to leave room for the stack
    unsigned int pagesNeeded = divRoundUp(size, PageSize);

    if ((int)pagesNeeded > memoryManager->freePages()) {
	DEBUG('a', "Not enough memory: need %d pages, %d free\n",
		pagesNeeded, memoryManager->freePages());
	return;
    }

    numPages = pagesNeeded;
    size = numPages * PageSize;
    DEBUG('a', "Initializing address space, num pages %d, size %d\n",
					numPages, size);

// set up the translation: each virtual page -> a free physical page
    pageTable = new TranslationEntry[numPages];
    for (i = 0; i < numPages; i++) {
	int phys = memoryManager->getPage();
	if (phys < 0) {			// should not happen after the check
	    for (unsigned int j = 0; j < i; j++)	// give back what we got
		memoryManager->clearPage(pageTable[j].physicalPage);
	    delete [] pageTable;
	    pageTable = NULL;
	    numPages = 0;
	    return;
	}
	pageTable[i].virtualPage = i;
	pageTable[i].physicalPage = phys;
	pageTable[i].valid = TRUE;
	pageTable[i].use = FALSE;
	pageTable[i].dirty = FALSE;
	pageTable[i].readOnly = FALSE;

	// zero just this page (uninitialized data and stack start as 0)
	bzero(&(machine->mainMemory[phys * PageSize]), PageSize);
    }

// then, copy in the code and data segments through the page table
    if (noffH.code.size > 0) {
        DEBUG('a', "Initializing code segment, at 0x%x, size %d\n",
			noffH.code.virtualAddr, noffH.code.size);
	if (!ReadFile(noffH.code.virtualAddr, executable,
		      noffH.code.size, noffH.code.inFileAddr))
	    return;			// destructor frees the pages
    }
    if (noffH.initData.size > 0) {
        DEBUG('a', "Initializing data segment, at 0x%x, size %d\n",
			noffH.initData.virtualAddr, noffH.initData.size);
	if (!ReadFile(noffH.initData.virtualAddr, executable,
		      noffH.initData.size, noffH.initData.inFileAddr))
	    return;
    }

    valid = TRUE;
    printf("Loaded Program: %d code | %d data | %d bss\n",
	   noffH.code.size, noffH.initData.size, noffH.uninitData.size);
}

//----------------------------------------------------------------------
// AddrSpace::AddrSpace(AddrSpace *parent)
// 	Used by Fork. Build a copy of "parent": the same number of pages,
//	each one a newly allocated physical page holding a copy of the
//	parent's page. After this, parent and child share nothing.
//
//	If there is not enough free memory, nothing is allocated and
//	IsValid() returns FALSE.
//----------------------------------------------------------------------

AddrSpace::AddrSpace(AddrSpace *parent)
{
    valid = FALSE;
    pageTable = NULL;
    numPages = 0;
    pcb = NULL;

    if ((int)parent->numPages > memoryManager->freePages()) {
	DEBUG('a', "Fork: not enough memory: need %d pages, %d free\n",
		parent->numPages, memoryManager->freePages());
	return;
    }

    numPages = parent->numPages;
    pageTable = new TranslationEntry[numPages];
    for (unsigned int i = 0; i < numPages; i++) {
	int phys = memoryManager->getPage();
	if (phys < 0) {			// should not happen after the check
	    for (unsigned int j = 0; j < i; j++)
		memoryManager->clearPage(pageTable[j].physicalPage);
	    delete [] pageTable;
	    pageTable = NULL;
	    numPages = 0;
	    return;
	}
	pageTable[i] = parent->pageTable[i];	// same flags
	pageTable[i].physicalPage = phys;
	memcpy(&(machine->mainMemory[phys * PageSize]),
	       &(machine->mainMemory[parent->pageTable[i].physicalPage * PageSize]),
	       PageSize);
    }

    valid = TRUE;
}

//----------------------------------------------------------------------
// AddrSpace::~AddrSpace
// 	Give every physical page back to the MemoryManager.
//----------------------------------------------------------------------

AddrSpace::~AddrSpace()
{
    if (pageTable != NULL) {
	for (unsigned int i = 0; i < numPages; i++)
	    memoryManager->clearPage(pageTable[i].physicalPage);
	delete [] pageTable;
    }
}

//----------------------------------------------------------------------
// AddrSpace::Translate
// 	Convert a virtual address in this space to a physical address
//	in machine->mainMemory. Returns FALSE if it is not mapped.
//----------------------------------------------------------------------

bool
AddrSpace::Translate(int virtAddr, int *physAddr)
{
    if (virtAddr < 0)
	return FALSE;

    unsigned int vpn = (unsigned int) virtAddr / PageSize;
    unsigned int offset = (unsigned int) virtAddr % PageSize;

    if (vpn >= numPages || !pageTable[vpn].valid)
	return FALSE;

    *physAddr = pageTable[vpn].physicalPage * PageSize + offset;
    return TRUE;
}

//----------------------------------------------------------------------
// AddrSpace::ReadFile
// 	Load "size" bytes from "file" (starting at "fileAddr") into this
//	address space at virtual address "virtAddr". The bytes are copied
//	one page-sized piece at a time, because consecutive virtual pages
//	are not consecutive in physical memory any more.
//----------------------------------------------------------------------

bool
AddrSpace::ReadFile(int virtAddr, OpenFile *file, int size, int fileAddr)
{
    char *buffer = new char[size];

    if (file->ReadAt(buffer, size, fileAddr) != size) {
	delete [] buffer;
	return FALSE;
    }

    int copied = 0;
    while (copied < size) {
	int physAddr;
	if (!Translate(virtAddr + copied, &physAddr)) {
	    delete [] buffer;
	    return FALSE;
	}
	int offset = (virtAddr + copied) % PageSize;
	int chunk = PageSize - offset;		// rest of this page
	if (chunk > size - copied)
	    chunk = size - copied;
	memcpy(&(machine->mainMemory[physAddr]), buffer + copied, chunk);
	copied += chunk;
    }

    delete [] buffer;
    return TRUE;
}

static bool
ValidExecSegment(const Segment& segment, int fileLength, bool fileBacked)
{
    if (segment.size < 0 || segment.virtualAddr < 0)
        return FALSE;

    if (segment.virtualAddr > 0x7fffffff - segment.size)
        return FALSE;

    if (fileBacked) {
        if (segment.inFileAddr < 0 ||
            segment.inFileAddr > fileLength ||
            segment.size > fileLength - segment.inFileAddr)
            return FALSE;
    }

    return TRUE;
}

static bool
CopyBufferToPageTable(TranslationEntry* table, unsigned int pageCount,
    int virtAddr, const char* buffer, int size)
{
    int copied = 0;

    while (copied < size) {
        unsigned int address = (unsigned int)virtAddr + copied;
        unsigned int vpn = address / PageSize;
        int offset = address % PageSize;

        if (vpn >= pageCount)
            return FALSE;

        int chunk = PageSize - offset;
        if (chunk > size - copied)
            chunk = size - copied;

        int physicalAddress =
            table[vpn].physicalPage * PageSize + offset;

        memcpy(&machine->mainMemory[physicalAddress],
            buffer + copied, chunk);
        copied += chunk;
    }

    return TRUE;
}

bool
AddrSpace::ReplaceFrom(OpenFile* executable)
{
    if (executable == NULL || !valid || memoryManager == NULL)
        return FALSE;

    NoffHeader noffH;
    if (executable->ReadAt((char*)&noffH, sizeof(noffH), 0)
        != sizeof(noffH))
        return FALSE;

    if ((noffH.noffMagic != NOFFMAGIC) &&
        (WordToHost(noffH.noffMagic) == NOFFMAGIC))
        SwapHeader(&noffH);

    if (noffH.noffMagic != NOFFMAGIC)
        return FALSE;

    // Clear unused segment addresses
    // The NOFF converter seemed to be leaving them uninitialized
    if (noffH.initData.size == 0) {
        noffH.initData.virtualAddr = 0;
        noffH.initData.inFileAddr = 0;
    }
    if (noffH.uninitData.size == 0) {
        noffH.uninitData.virtualAddr = 0;
        noffH.uninitData.inFileAddr = 0;
    }

    int fileLength = executable->Length();

    if (!ValidExecSegment(noffH.code, fileLength, TRUE) ||
        !ValidExecSegment(noffH.initData, fileLength, TRUE) ||
        !ValidExecSegment(noffH.uninitData, fileLength, FALSE))
        return FALSE;

    unsigned int codeEnd =
        noffH.code.virtualAddr + noffH.code.size;
    unsigned int dataEnd =
        noffH.initData.virtualAddr + noffH.initData.size;
    unsigned int bssEnd =
        noffH.uninitData.virtualAddr + noffH.uninitData.size;

    unsigned int imageEnd = codeEnd;
    if (dataEnd > imageEnd) imageEnd = dataEnd;
    if (bssEnd > imageEnd) imageEnd = bssEnd;

    if (imageEnd > 0x7fffffffU - UserStackSize)
        return FALSE;

    unsigned int requiredBytes = imageEnd + UserStackSize;
    unsigned int newPageCount = divRoundUp(requiredBytes, PageSize);

    if (newPageCount == 0 || newPageCount > NumPhysPages)
        return FALSE;

    // Read all file-backed bytes before reusing any of the old frames
    char* codeBuffer = NULL;
    char* dataBuffer = NULL;

    if (noffH.code.size > 0) {
        codeBuffer = new char[noffH.code.size];
        if (executable->ReadAt(codeBuffer, noffH.code.size,
            noffH.code.inFileAddr) != noffH.code.size) {
            delete[] codeBuffer;
            return FALSE;
        }
    }

    if (noffH.initData.size > 0) {
        dataBuffer = new char[noffH.initData.size];
        if (executable->ReadAt(dataBuffer, noffH.initData.size,
            noffH.initData.inFileAddr)
            != noffH.initData.size) {
            delete[] codeBuffer;
            delete[] dataBuffer;
            return FALSE;
        }
    }

    int* oldFrames = new int[numPages];
    int* newFrames = new int[newPageCount];
    TranslationEntry* newTable =
        new TranslationEntry[newPageCount];

    for (unsigned int i = 0; i < numPages; i++)
        oldFrames[i] = pageTable[i].physicalPage;

    if (!memoryManager->ReplacePages(oldFrames, numPages,
        newPageCount, newFrames)) {
        delete[] oldFrames;
        delete[] newFrames;
        delete[] newTable;
        delete[] codeBuffer;
        delete[] dataBuffer;
        return FALSE;
    }

    for (unsigned int i = 0; i < newPageCount; i++) {
        newTable[i].virtualPage = i;
        newTable[i].physicalPage = newFrames[i];
        newTable[i].valid = TRUE;
        newTable[i].use = FALSE;
        newTable[i].dirty = FALSE;
        newTable[i].readOnly = FALSE;

        memset(&machine->mainMemory[newFrames[i] * PageSize],
            0, PageSize);
    }

    // These copies should not fail after the segment ranges and page count have been validated above
    if (noffH.code.size > 0)
        ASSERT(CopyBufferToPageTable(newTable, newPageCount,
            noffH.code.virtualAddr,
            codeBuffer, noffH.code.size));

    if (noffH.initData.size > 0)
        ASSERT(CopyBufferToPageTable(newTable, newPageCount,
            noffH.initData.virtualAddr,
            dataBuffer, noffH.initData.size));

    TranslationEntry* oldTable = pageTable;
    pageTable = newTable;
    numPages = newPageCount;
    valid = TRUE;

    delete[] oldTable;
    delete[] oldFrames;
    delete[] newFrames;
    delete[] codeBuffer;
    delete[] dataBuffer;

    printf("Loaded Program: %d code | %d data | %d bss\n",
        noffH.code.size, noffH.initData.size,
        noffH.uninitData.size);

    return TRUE;
}

#else

//----------------------------------------------------------------------
// AddrSpace::AddrSpace
// 	Create an address space to run a user program.
//	Load the program from a file "executable", and set everything
//	up so that we can start executing user instructions.
//
//	Assumes that the object code file is in NOFF format.
//
//	First, set up the translation from program memory to physical
//	memory.  For now, this is really simple (1:1), since we are
//	only uniprogramming, and we have a single unsegmented page table
//
//	"executable" is the file containing the object code to load into memory
//----------------------------------------------------------------------

AddrSpace::AddrSpace(OpenFile *executable)
{
    NoffHeader noffH;
    unsigned int i, size;

    executable->ReadAt((char *)&noffH, sizeof(noffH), 0);
    if ((noffH.noffMagic != NOFFMAGIC) &&
		(WordToHost(noffH.noffMagic) == NOFFMAGIC))
    	SwapHeader(&noffH);
    ASSERT(noffH.noffMagic == NOFFMAGIC);

// how big is address space?
    size = noffH.code.size + noffH.initData.size + noffH.uninitData.size
			+ UserStackSize;	// we need to increase the size
						// to leave room for the stack
    numPages = divRoundUp(size, PageSize);
    size = numPages * PageSize;

    ASSERT(numPages <= NumPhysPages);		// check we're not trying
						// to run anything too big --
						// at least until we have
						// virtual memory

    DEBUG('a', "Initializing address space, num pages %d, size %d\n",
					numPages, size);
// first, set up the translation
    pageTable = new TranslationEntry[numPages];
    for (i = 0; i < numPages; i++) {
	pageTable[i].virtualPage = i;	// for now, virtual page # = phys page #
	pageTable[i].physicalPage = i;
	pageTable[i].valid = TRUE;
	pageTable[i].use = FALSE;
	pageTable[i].dirty = FALSE;
	pageTable[i].readOnly = FALSE;  // if the code segment was entirely on
					// a separate page, we could set its
					// pages to be read-only
    }

// zero out the entire address space, to zero the unitialized data segment
// and the stack segment
    bzero(machine->mainMemory, size);

// then, copy in the code and data segments into memory
    if (noffH.code.size > 0) {
        DEBUG('a', "Initializing code segment, at 0x%x, size %d\n",
			noffH.code.virtualAddr, noffH.code.size);
        executable->ReadAt(&(machine->mainMemory[noffH.code.virtualAddr]),
			noffH.code.size, noffH.code.inFileAddr);
    }
    if (noffH.initData.size > 0) {
        DEBUG('a', "Initializing data segment, at 0x%x, size %d\n",
			noffH.initData.virtualAddr, noffH.initData.size);
        executable->ReadAt(&(machine->mainMemory[noffH.initData.virtualAddr]),
			noffH.initData.size, noffH.initData.inFileAddr);
    }

}

//----------------------------------------------------------------------
// AddrSpace::~AddrSpace
// 	Dealloate an address space.  Nothing for now!
//----------------------------------------------------------------------

AddrSpace::~AddrSpace()
{
   delete pageTable;
}

#endif

//----------------------------------------------------------------------
// AddrSpace::InitRegisters
// 	Set the initial values for the user-level register set.
//
// 	We write these directly into the "machine" registers, so
//	that we can immediately jump to user code.  Note that these
//	will be saved/restored into the currentThread->userRegisters
//	when this thread is context switched out.
//----------------------------------------------------------------------

void
AddrSpace::InitRegisters()
{
    int i;

    for (i = 0; i < NumTotalRegs; i++)
	machine->WriteRegister(i, 0);

    // Initial program counter -- must be location of "Start"
    machine->WriteRegister(PCReg, 0);

    // Need to also tell MIPS where next instruction is, because
    // of branch delay possibility
    machine->WriteRegister(NextPCReg, 4);

   // Set the stack register to the end of the address space, where we
   // allocated the stack; but subtract off a bit, to make sure we don't
   // accidentally reference off the end!
    machine->WriteRegister(StackReg, numPages * PageSize - 16);
    DEBUG('a', "Initializing stack register to %d\n", numPages * PageSize - 16);
}

//----------------------------------------------------------------------
// AddrSpace::SaveState
// 	On a context switch, save any machine state, specific
//	to this address space, that needs saving.
//
//	For now, nothing!
//----------------------------------------------------------------------

void AddrSpace::SaveState()
{}

//----------------------------------------------------------------------
// AddrSpace::RestoreState
// 	On a context switch, restore the machine state so that
//	this address space can run.
//
//      For now, tell the machine where to find the page table.
//----------------------------------------------------------------------

void AddrSpace::RestoreState()
{
    machine->pageTable = pageTable;
    machine->pageTableSize = numPages;
}
