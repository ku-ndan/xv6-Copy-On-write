#ifndef PAGING_H
#define PAGING_H
#define PTE_SWAPPED     0x200

void handle_pgfault();
pte_t* select_a_victim(pde_t *pgdir);
void clearaccessbit(pde_t *pgdir);
int getswappedblk(pde_t *pgdir, uint va);
int swap_page();
int swap_page_from_pte(pte_t *pte);
void map_address(struct proc *p, uint addr);

#endif
