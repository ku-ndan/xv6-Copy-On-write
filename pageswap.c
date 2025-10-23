#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "pageswap.h"
#include "fs.h"

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
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

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    // if(*pte & PTE_P)
    //   panic("remap in mappages in paging.c");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

//Allocate eight consecutive disk blocks and store the content of a 
//physical page in the corresponding page table entry (PTE).

int 
swap_page_from_pte(pte_t *pte)
{
    if(!((*pte) & PTE_P))
        panic("swapping page which is not present.");
    uint pa=PTE_ADDR(*pte);  
    uint diskPagestart=balloc_page(ROOTDEV);

    write_page_to_disk((char*)P2V(pa),diskPagestart);    
    kfree(P2V(pa));
    *pte = (diskPagestart << 12) | PTE_SWAPPED;
    *pte = *pte & ~PTE_P;
    return diskPagestart;
}

// Select a victim and swap the contents to the disk.
int
swap_page()
{
    struct proc *p = find_victim_process();
    pde_t *pgdir = p->pgdir;
    pte_t *pte = select_a_victim(pgdir);
    // for loop apply ---------------------------------------------------------------

    int diskps = swap_page_from_pte(pte);

    if(p->rss > 1) p->rss-=PGSIZE;
    lcr3(V2P(pgdir));  
    return diskps;
}


/* Map a physical page to the virtual address addr. If the page table entry points to a swapped block then
restore the content of the page from the swapped block and free the swapped block.*/
void
map_address(struct proc *p, uint addr)
{
    pte_t *pte = walkpgdir(p->pgdir, (char*)addr, 0);  //taking entry from cr2 which is basically addr only. rounds the address in multiple of page size (PGSIZE)
    int blockid = -1;   //disk id where the page was swapped
    char *mem;
    int diskps;
    if((mem = kalloc()) == 0){
      diskps = swap_page();
      mem = kalloc();
      //cprintf("%d\n", diskps);
      //now a physical page has been swapped to disk and free, so this time we will 
      //get physical page for sure.
      //update_pte_proc(p->pgdir, addr, diskps);
      //cprintf("I am out\n");
    }
    uint cursz = p->sz;
    p->rss+=PGSIZE;
    if(pte!=0){
      if(*pte & PTE_SWAPPED){
        blockid = *(walkpgdir(p->pgdir,(char*)addr,0))>>12;
        read_page_from_disk(mem, blockid);
        //cprintf("I am out2\n");
        //update_pte_proc1(p->pgdir, addr, mem);
        *pte=V2P(mem) | PTE_W | PTE_U | PTE_P;
        *pte &= ~PTE_SWAPPED;
        // uint pa = PTE_ADDR(*pte);
        // p->parent->rss+=PGSIZE;
        lcr3(V2P(p->pgdir));
        //cprintf("I have reached here");
        bfree_page(ROOTDEV,blockid);
      } else {
        memset(mem,0,PGSIZE);
        if(mappages(p->pgdir, (char*)addr, PGSIZE, V2P(mem), PTE_P | PTE_W | PTE_U)<0){
            deallocuvm(p->pgdir,cursz+PGSIZE, cursz);
            kfree(mem);
            p->rss-=PGSIZE;
        }
      }
    }
    // cprintf("[MAP_ADDRESS] in pagefault for not present page");
    if(p->parent->rss > p->rss) p->parent->rss-=PGSIZE;
    // p->parent->rss-=PGSIZE;
}

/* page fault handler */
void
handle_pgfault()
{
	unsigned addr;
	struct proc *curproc = myproc();
	asm volatile ("movl %%cr2, %0 \n\t" : "=r" (addr));
	addr &= ~0xfff;

	map_address(curproc, addr);
  
}


