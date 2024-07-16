#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "proc.h"
#include "spinlock.h"

/*
 * the kernel's page table.
 * codes for manipulating address spaces and page tables
 * function starts with kvm: manage kernel page table
 * function starts with uvm: manage user page table
 * other functions are used for both
 */
pagetable_t kernel_pagetable; //a pointer to a RISC-V root page-table page, can be either kernel or process pgtbl

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  //allocate a page of physical memory to hold the root page-table page
  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  //using these kvmmap to install the translations that the kernel needs
  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // map kernel stacks, allocates a kernel stack for each processes
  proc_mapstacks(kpgtbl);
  
  return kpgtbl;
}

// Initialize the one kernel_pagetable
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

//main() calls this to install the kernel page table
// Switch h/w page table register to the kernel's page table,
// and ENABLES PAGING
void
kvminithart()
{
  //write the address of root page-table page to satp
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma(); //flushes the current CPU's TLB
}

//mimics the RISC-V paging hardware as it looks up the PTE for a virtual address
// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.
// alloc indicates whether to allocate new page-table pages if they do not already exist
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA) //out of bound
    panic("walk");

  for(int level = 2; level > 0; level--) {
    //extract from va the index for the given level of page table
    pte_t *pte = &pagetable[PX(level, va)];

    if(*pte & PTE_V) {  //if current PTE is valid
      //set pagetable to the physical address of next level page table
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      //if alloc is 0 or kalloc() fail
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      //if alloc is set, allocates a new page-table page and put its physical address in the PTE
      memset(pagetable, 0, PGSIZE);

      //Update the current PTE to point to the newly allocated page-table page and mark it as valid
      //thus ready to traverse to the next level of page table
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  //return the physical address the PTE at lowest level points to
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0); //find the PTE in the bottom level pagetable
  if(pte == 0)  //walk failed
    return 0;
  if((*pte & PTE_V) == 0) //invalid PTE
    return 0;
  if((*pte & PTE_U) == 0) //PTE not for user
    return 0;
  pa = PTE2PA(*pte);  //translate PTE to physical address
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if(size == 0)
    panic("mappages: size");
  
  a = PGROUNDDOWN(va);  //round down to nearest page boundary(alignment)
  last = PGROUNDDOWN(va + size - 1);
  for(;;){  //loop through each page between a and last
    //walk() get the PTE for current address a
    if((pte = walk(pagetable, a, 1)) == 0)  //walk() fails to allocate a needed page-table page
      return -1;
    if(*pte & PTE_V)  //current pgtbl is valid(already mapped)
      panic("mappages: remap");

    *pte = PA2PTE(pa) | perm | PTE_V; //set up PTE

    if(a == last) //last page reached
      break;
  
    a += PGSIZE;  //move to next page
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      //panic("uvmunmap: walk");
      continue; //lab5a
    if((*pte & PTE_V) == 0)
      //panic("uvmunmap: not mapped");
      continue; //lab5a
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

//lab5a
void
uvmlazyalloc(uint64 faultva)
{
  struct proc *p = myproc();
  pagetable_t pagetable = p->pagetable;
  char *mem;
  if((mem = kalloc()) == 0){
    printf("lazy: faild to allocate memory");
    p->killed = 1;  //kill process
  }
  //basically copy from uvmalloc
  memset(mem, 0, PGSIZE); //fill new page with 0
  //map allocated physical memory to virtual address a
  if(mappages(pagetable, PGROUNDDOWN(faultva), PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
    printf("lazy: failed to map newly allocated page");
    kfree(mem); //frees the allocated memory
    p->killed = 1;  //kill process
  }
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz; //does nothing because this function does not handle shrinking

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){ //is kalloc fails
      uvmdealloc(pagetable, a, oldsz);  //free any previous allocated page
      return 0;
    }
    memset(mem, 0, PGSIZE); //fill newly allocated page with 0

    //map allocated physical memory to virtual address a
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      //if mapping failed
      kfree(mem); //frees the allocated memory
      uvmdealloc(pagetable, a, oldsz);  //free previously allocated pages
      return 0;
    }
  }
  //all kalloc() and mappages() are successful
  return newsz; 
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz; //does nothing because this function does not handle growing

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){  //if they do not round up to the same boundary
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;  //how many pages need to be freed
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1); //unmap those pages
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    //if none of R, W, and X are set, then this PTE is not a leaf
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}

void
vmprint(pagetable_t pagetable, uint64 depth){
  if(depth > 2){ //there's only 3 level
    return;
  }
  if(depth == 0){ //first print the argument to vmprint(which is the root)
    printf("page table %p\n", pagetable);
  }
  
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if(pte & PTE_V){  //only print PTE that is valid
      switch (depth){ //print .. based on level
        case 0:
          printf("..");
          break;
        case 1:
          printf(".. ..");
          break;
        case 2:
          printf(".. .. ..");
          break;
      }
      printf("%d: pte %p pa %p\n", i, pte, PTE2PA(pte));  //print info
      uint64 child = PTE2PA(pte); //address of the root of next level
      vmprint((pagetable_t) child, depth + 1);
    }
  }
}


int
accessed_page(pagetable_t pagetable, uint64 va){
  pte_t *pte;

  if(va >= MAXVA)
    return 0;
  pte = walk(pagetable, va, 0); //find the physical address of cur page
  if(pte == 0)
    return 0;
  if((*pte & PTE_A) != 0){  //if the page is accessed
    *pte &= ~PTE_A; //set access flag to 0
    return 1;
  }
  return 0;  
}