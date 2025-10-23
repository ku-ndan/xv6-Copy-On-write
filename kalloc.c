// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "pageswap.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "pageswap.h"
#include "fs.h"
#define NFRAMES PHYSTOP/PGSIZE          //number of frames
#define PGSHIFT         12      // log2(PGSIZE)

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  uint num_free_pages;  //store number of free pages
  struct run *freelist;
} kmem;

struct{                             //reference count structure
  int rcount[NFRAMES];
  int procs[NFRAMES][NPROC];
  struct proc* p;
  struct spinlock lock;
} rmap;

static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

void update_pte_proc(pde_t *pde, uint addr, uint diskPagestart){
  pte_t *pte = walkpgdir(pde, (char*)addr, 0);
  uint pa=PTE_ADDR(*pte);
  for(int i=0;i<NPROC;i++){
      // cprintf("Hi");
    // for(int k = 0; k< NFRAMES ; k++){                               //initialize reference count to 0
    //   for (int j = 0; j < NPROC; j++) {
    //     if(rmap.procs[i][j] > 1) cprintf("%d\n",rmap.procs[i][j]);
    //   }
    // }
    // cprintf("%d\n", pa>>PGSHIFT);
    if(rmap.procs[pa>>PGSHIFT][i] == 1){
      cprintf("%d\n",rmap.procs[pa>>PGSHIFT][i]);
      cprintf("Inside\n");
      struct proc* p = findproc_with_pid(i);
      pte_t *pte1 = walkpgdir(p->pgdir, (char*)addr, 0);
      //
      *pte1 = (diskPagestart << 12) |  PTE_SWAPPED;
      *pte1 = *pte1 & ~PTE_P;
      p->rss+=4096;
    }
    
  }
  return;
}

void update_pte_proc1(pde_t *pde, uint addr, char *mem){
  pte_t *pte = walkpgdir(pde, (char*)addr, 0);
  uint pa=PTE_ADDR(*pte);
  for(int i=0;i<NPROC;i++){
    if(rmap.procs[pa>>PGSHIFT][i] == 1){
      struct proc* p = findproc_with_pid(i);
      if(p->pgdir!=pde){
        pte_t *pte1 = walkpgdir(p->pgdir, (char*)addr, 0);
      *pte1=V2P(mem) | PTE_W | PTE_U | PTE_P;
      *pte1 &= ~PTE_SWAPPED;
      }
      
    }
  }
}
void
rinit(void)                       //initialize reference count structure
{
  initlock(&rmap.lock, "rmap");
  acquire(&rmap.lock);
  int i;
  int j;
  for(i = 0; i< NFRAMES ; i++){                               //initialize reference count to 0
    rmap.rcount[i] = 0; 
    for (j = 0; j < NPROC; j++) {
      rmap.procs[i][j] = 0;
  }
  }
  
  // cprintf("\ncpu%d: rinit called\n\n", cpunum());
  release(&rmap.lock);
}
  
void incrementRcount(uint pa, int pid){                    //increment reference count
  acquire(&rmap.lock);
  rmap.rcount[pa>>PGSHIFT]++;
  rmap.procs[pa>>PGSHIFT][pid] = 1;

  release(&rmap.lock);
}
void decrementRcount(uint pa, int pid){                  //decrement reference count
  acquire(&rmap.lock);
  rmap.rcount[pa>>PGSHIFT]--;
  rmap.procs[pa>>PGSHIFT][pid] = 0;
  release(&rmap.lock);
}


int getRcount(uint pa){                         //get reference count
  acquire(&rmap.lock);
  uint temp = rmap.rcount[pa>>PGSHIFT];
  release(&rmap.lock);
  return temp;
}

/*Lock must be held*/
void setRcount(uint pa,int i){                //set reference count
  rmap.rcount[pa>>PGSHIFT] = i;
}





// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  
  kmem.num_free_pages = 0;

  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
  {
    kfree(p);
  }
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  if(kmem.use_lock){
    acquire(&kmem.lock);
    acquire(&rmap.lock);
  }
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;
  kmem.num_free_pages++;                            //increment free page count
  setRcount((uint)V2P((uint)r),0);                //set reference count to 0
  if(kmem.use_lock){
    release(&kmem.lock);
    release(&rmap.lock);
  }
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;

  if(kmem.use_lock){
    acquire(&kmem.lock);
    // acquire(&rmap.lock);
  }
  r = kmem.freelist;
  if(r)
  {
    kmem.freelist = r->next;
    kmem.num_free_pages-=1;
    setRcount((uint)V2P((uint)r),1);             //set reference count to 1
  }
    
  if(kmem.use_lock){
    release(&kmem.lock);
    // release(&rmap.lock);
  }
  return (char*)r;
}
uint 
num_of_FreePages(void)
{
  acquire(&kmem.lock);

  uint num_free_pages = kmem.num_free_pages;
  
  release(&kmem.lock);
  
  return num_free_pages;
}

