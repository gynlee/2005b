#include "copyright.h"
#include "tlb.h"
#include "syscall.h"
#include "machine.h"
#include "thread.h"
#include "system.h"
#include "utility.h"

//----------------------------------------------------------------------
// UpdateTLB
//      Called when exception is raised and a page isn't in the TLB.
// Figures out what to do (get from IPT, or pageoutpagein) and does it.
//----------------------------------------------------------------------

void UpdateTLB(int possible_badVAddr)
{
  int badVAddr;
  unsigned int vpn;
  int phyPage;

  if(possible_badVAddr) // get the bad address from the correct location
    badVAddr=possible_badVAddr; // fault in kernel
  else
    badVAddr=machine->registers[BadVAddrReg]; // fault in userprog
  
  vpn=(unsigned)badVAddr/PageSize;
  
  if((phyPage=VpnToPhyPage(vpn))!=-1) {
    InsertToTLB(vpn, phyPage);
  } else {
    if(vpn>=currentThread->space->numPages && !GetMmap(vpn))
      machine->RaiseException(AddressErrorException, badVAddr);
    else
      InsertToTLB(vpn, PageOutPageIn(vpn));
  }
}

//----------------------------------------------------------------------
// VpnToPhyPage
//      Gets a phyPage for a vpn, if exists in ipt.
//----------------------------------------------------------------------

int VpnToPhyPage(int vpn)
{
  //your code here to get a physical frame for page vpn
  //you can refer to PageOutPageIn(int vpn) to see how an entry was created in ipt

  printf("--- printf: VpnToPhyPage(vpn=%i)\n", vpn);
  //DEBUG('p', "--- DEBUG: VpnToPhyPage(vpn=%d)\n", vpn);
  
  int phyPage = -1; //return -1 if not found

  for(int i = 0; i < NumPhysPages; i++) {
    
    //if found, update phyPage to value of found index
    if(memoryTable[i].valid && 
       memoryTable[i].pid == currentThread->pid && 
       memoryTable[i].vPage == vpn) {
      
      printf("--- printf: VpnToPhyPage found=%i\n", i);
      //DEBUG('p', "--- DEBUG: VpnToPhyPage found=%d\n", i);
      
      phyPage = i;
      break;
    }
  }
  
  printf("--- printf: phyPage=%i\n", phyPage);
  //DEBUG('p', "--- DEBUG: phyPage=%d\n", phyPage);
  
  return phyPage;
}

//----------------------------------------------------------------------
// InsertToTLB
//      Put a vpn/phyPage combination into the TLB. If TLB is full, use FIFO 
//    replacement
//----------------------------------------------------------------------

void InsertToTLB(int vpn, int phyPage)
{
  static int FIFOPointer = 0;
  int i = 0; //entry in the TLB
  
  //your code to find an empty in TLB or to replace the oldest entry if TLB is full
  
  printf("--- printf: InsertToTLB(vpn=%i, phyPage=%i)\n", vpn, phyPage);
  //DEBUG('p', "--- DEBUG: InsertToTLB(vpn=%i, phyPage=%i)\n", vpn, phyPage);
  
  int invalidEntry = -1;

  //search for invalid entry
  for(i = 0; i < TLBSize; i++) {
    if(!machine->tlb[i].valid) {
      printf("--- printf: invalid TLB entry found = machine->tlb[%i]\n", i);
      //DEBUG('p', "--- DEBUG: invalid TLB entry found = machine->tlb[%i]\n", i);
      
      //update invalidEntry to value of found index
      invalidEntry = i;
      break;
    }
  }
  
  //all valid, use FIFO
  if(invalidEntry < 0) {
    printf("--- printf: invalidEntry not found.\n");
    //DEBUG('p', "--- DEBUG: invalidEntry not found.\n");
    
    printf("--- printf: using oldest entry=%i.\n", FIFOPointer);
    //DEBUG('p', "--- using oldest entry=%i.\n", FIFOPointer);
    i = FIFOPointer;
  }
  
  //update FIFO pointer
  FIFOPointer = (i + 1) % TLBSize;
  printf("\n--- printf: new FIFOPointer = %i.\n", FIFOPointer);
  //DEBUG('p', "\n--- new FIFOPointer = %i.\n", FIFOPointer);
  
  
  // copy dirty data to memoryTable
  if(machine->tlb[i].valid){
    memoryTable[machine->tlb[i].physicalPage].dirty=machine->tlb[i].dirty;
    memoryTable[machine->tlb[i].physicalPage].TLBentry=-1;
  }

  //update the TLB entry
  machine->tlb[i].virtualPage  = vpn;
  machine->tlb[i].physicalPage = phyPage;
  machine->tlb[i].valid        = TRUE;
  machine->tlb[i].readOnly     = FALSE;
  machine->tlb[i].use          = FALSE;
  machine->tlb[i].dirty        = memoryTable[phyPage].dirty;

  //update the corresponding memoryTable
  memoryTable[phyPage].TLBentry=i;
  DEBUG('p', "\nThe corresponding TLB entry for Page %i in TLB is %i \n", vpn, i);
  //reset lastUsed to current ticks since it is being used at this moment.
  //for the implementation of LRU algorithm.
  memoryTable[phyPage].lastUsed = stats->totalTicks; 
  
  //increase the number of tlb misses
  stats->numTlbMisses++;
  

}

//----------------------------------------------------------------------
// PageOutPageIn
//      Calls DoPageOut and DoPageIn and handles memoryTable
// bookkeeping. Use lru algorithm to find the replacement page.
//----------------------------------------------------------------------

int PageOutPageIn(int vpn)
{
  int phyPage; 
  
  //print IPT table//
  printf("\n--- [IPT] ---\n"); 
  for(int x = 0; x < NumPhysPages; x++) { 
    printf("IPT[%i]: pid=%i, vpn=%i, last used=%i, valid=%i\n", 
    	x, memoryTable[x].pid, memoryTable[x].vPage, memoryTable[x].lastUsed, memoryTable[x].valid); 
  } 
  printf("--- [IPT] ---\n");
  
  //print TLB table//
  printf("\n--- [TLB] ---\n"); 
  for(int x = 0; x < TLBSize; x++) { 
    printf("TLB[%i]: vpn=%i, phy=%i, valid=%i\n", 
    	x, machine->tlb[x].virtualPage, machine->tlb[x].physicalPage, machine->tlb[x].valid); 
  } 
  printf("--- [TLB] ---\n"); printf("\n");
  

  //increase the number of page faults
  stats->numPageFaults++;
  //call the LRU algorithm, which returns the freed physical frame
  phyPage=lruAlgorithm();
  
  //Page out the victim page to free the physical frame
  DoPageOut(phyPage);
  //Page in the new page to the freed physical frame
  DoPageIn(vpn, phyPage);
  
  //update memoryTable for this frame
  memoryTable[phyPage].valid=TRUE;
  memoryTable[phyPage].pid=currentThread->pid;
  memoryTable[phyPage].vPage=vpn;
  memoryTable[phyPage].dirty=FALSE;
  memoryTable[phyPage].TLBentry=-1;
  memoryTable[phyPage].lastUsed=0;
  memoryTable[phyPage].swapPtr=currentThread->space->swapPtr;
  
  return phyPage;
}

//----------------------------------------------------------------------
// DoPageOut
//      Actually pages out a phyPage to it's swapfile.
//----------------------------------------------------------------------

void DoPageOut(int phyPage)
{
  MmapEntry *mmapPtr;
  int numBytesWritten;
  int mmapBytesToWrite;

  if(memoryTable[phyPage].valid){           // check if pageOut possible
    if(memoryTable[phyPage].TLBentry!=-1){
      memoryTable[phyPage].dirty = machine->tlb[memoryTable[phyPage].TLBentry].dirty;
      machine->tlb[memoryTable[phyPage].TLBentry].valid=FALSE;
    }
    if(memoryTable[phyPage].dirty){        // pageOut is necessary
      if((mmapPtr=GetMmap(memoryTable[phyPage].vPage))){ // it's mmaped
        DEBUG('p', "mmap paging out: pid %i, phyPage %i, vpn %i\n",
          memoryTable[phyPage].pid, phyPage, memoryTable[phyPage].vPage);
        if(memoryTable[phyPage].vPage==mmapPtr->endPage)
          mmapBytesToWrite=mmapPtr->lastPageLength;
        else
          mmapBytesToWrite=PageSize;
        numBytesWritten=mmapPtr->openFile->
          WriteAt(machine->mainMemory+phyPage*PageSize, mmapBytesToWrite,
            (memoryTable[phyPage].vPage-mmapPtr->beginPage)*PageSize);
        ASSERT(mmapBytesToWrite==numBytesWritten);
      } else { // it's not mmaped
        DEBUG('p', "paging out: pid %i, phyPage %i, vpn %i\n",
          memoryTable[phyPage].pid, phyPage, memoryTable[phyPage].vPage);
        numBytesWritten=memoryTable[phyPage].swapPtr->
          WriteAt(machine->mainMemory+phyPage*PageSize, PageSize,
            memoryTable[phyPage].vPage*PageSize);
        ASSERT(PageSize==numBytesWritten);
      }
      
      //increase the number of page faults
      stats->numPageOuts++;
    }
    
    memoryTable[phyPage].valid=FALSE;
  }
}

//----------------------------------------------------------------------
// DoPageIn
//      Actually pages in a phyPage/vpn combo from the swapfile.
//----------------------------------------------------------------------

void DoPageIn(int vpn, int phyPage)
{
  MmapEntry *mmapPtr;
  int numBytesRead;
  int mmapBytesToRead;

  if((mmapPtr=GetMmap(vpn))){ // mmaped file
    DEBUG('p', "mmap paging in: pid %i, phyPage %i, vpn %i\n",
      currentThread->pid, phyPage, vpn);
    if(vpn==mmapPtr->endPage)
      mmapBytesToRead=mmapPtr->lastPageLength;
    else
      mmapBytesToRead=PageSize;
    numBytesRead=
      mmapPtr->openFile->ReadAt(machine->mainMemory+phyPage*PageSize,
                mmapBytesToRead,
                (vpn-mmapPtr->beginPage)*PageSize);
    ASSERT(numBytesRead==mmapBytesToRead);
  } else { // not mmaped
    DEBUG('p', "paging in: pid %i, phyPage %i, vpn %i\n", currentThread->pid,
      phyPage, vpn);
    numBytesRead=currentThread->space->swapPtr->ReadAt(machine->mainMemory+
                   phyPage*PageSize,
                   PageSize,
                   vpn*PageSize);
    ASSERT(PageSize==numBytesRead);
  }
}

//----------------------------------------------------------------------
// lruAlgorithm
//      Determine where a vpn should go in phymem, and therefore what
// should be paged out. This lru algorithm is the one discussed in the 
// lectures.
//----------------------------------------------------------------------

int lruAlgorithm(void)
{
  //your code here to find the physical frame that should be freed 
  //according to the LRU algorithm. 
  int phyPage;
  
  printf("--- printf: lruAlgorithm()\n");
  //DEBUG('p', "--- DEBUG: lruAlgorithm()\n");
  
  phyPage = -1;
  
  int i = -1;

  //search for invalid algorithm
  for(i = 0; i < NumPhysPages; i++) {
    if(!memoryTable[i].valid) {
      printf("--- printf: invalid page table found ipt=%i\n", i);
      //DEBUG('p', "--- DEBUG: invalid page table found ipt=%d\n", i);
      
      //update phyPage to value found index
      phyPage = i;
      break;
    }
  }
  
  //if all valid, use the least recently used one
  if(phyPage < 0) {
    printf("--- printf: no invalid page table found, finding least recently used\n");
    //DEBUG('p', "--- DEBUG: no invalid page table found, finding least recently used\n");
    
    phyPage = 0;
    int lastUsed = memoryTable[phyPage].lastUsed;
    
    printf("--- printf: first last used, memoryTable[0].lastUsed=%i\n", lastUsed);
    //DEBUG('p', "--- DEBUG: first last used, memoryTable[0].lastUsed=%i\n", lastUsed);
    
   //compares all entries to find LRU 
   for(i = 0; i < NumPhysPages; i++) {
      printf("--- printf: memoryTable[%i].lastUsed=%i\n", i, memoryTable[i].lastUsed);
      //DEBUG('p', "--- DEBUG: memoryTable[%i].lastUsed=%i\n", i, memoryTable[i].lastUsed);
      if(memoryTable[i].lastUsed < lastUsed) {
        lastUsed = memoryTable[i].lastUsed;
		
        printf("--- printf: new last used found memoryTable[%i], new lastUsed=%i\n", i, lastUsed);
        //DEBUG('p', "--- DEBUG: new last used found memoryTable[%i], new lastUsed=%i\n", i, lastUsed);
         
        //update phyPage to value of found index
        phyPage = i;
      }
    }
  }
  
  printf("--- printf: lru ipt=%i\n", phyPage);
  //DEBUG('p', "--- DEBUG: lru ipt=%d\n", phyPage);
  
  return phyPage;
}

//----------------------------------------------------------------------
// GetMmap
//      Return an MmapEntry structure corresponding to the vpn.  Returns
// 0 if does not exist.
//----------------------------------------------------------------------

MmapEntry *GetMmap(int vpn)
{
  MmapEntry *mmapPtr;

  mmapPtr=currentThread->space->mmapEntries;
  while(mmapPtr->next){
    mmapPtr=mmapPtr->next;
    if(vpn>=mmapPtr->beginPage && vpn<=mmapPtr->endPage)
      return mmapPtr;
  }
  return 0;
}

//----------------------------------------------------------------------
// PageOutMmapSpace
//      Pages out stuff being mmaped (or just between beginPage and
// endPage.
//----------------------------------------------------------------------

void PageOutMmapSpace(int beginPage, int endPage)
{
  int vpn;
  int phyPage;

  for(vpn=beginPage; vpn<=endPage; vpn++){
    if((phyPage=VpnToPhyPage(vpn))==-1)
      continue;
    DoPageOut(phyPage);
  }
}
