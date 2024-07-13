// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

//lab5
//given a physical address, locate which page it is at
#define PA2PGID(pa) ((pa)-KERNBASE/PGSIZE)

//total number of pages, also the ID of the last physical page
#define NUMPAGES PA2PGID(PHYSTOP)

//an array to hold the cnt of references for each page
int pgrefcnt[NUMPAGES];

//given a pa, return the cnt of references for that page
#define PA2REFCNT(pa) pgrefcnt[PA2PGID((uint64)(pa))]

//a lock to prevent racing condition
struct spinlock pgreflock;


void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  //lab5: init lock for pgref
  initlock(&pgreflock, "pgref");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  //lab5: decrease the reference by 1
  acquire(&pgreflock);
  //only free the physical page if reference = 0
  if(--PA2REFCNT(pa) < 0){
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
  release(&pgreflock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    memset((char*)r, 5, PGSIZE);
    
    //lab5, initialize page reference
    PA2REFCNT(r) = 1; 
  } 
  return (void*)r;
}

void
increase_refcnt(void *pa){
  acquire(&pgreflock);
  PA2REFCNT(pa)++;
  release(&pgreflock);
}

void *
cowkalloc(void *pa)
{
  acquire(&pgreflock);
  //if reference cnt is 1, no need to allocate a new physical page
  if(PA2REFCNT(pa) <= 1){
    release(&pgreflock);
    return pa;
  }

  uint64 new;
  if(new = (uint64)(kalloc()) == 0){  //kalloc failed, out of memory
    release(&pgreflock);
    return 0;
  }
  //copy content of old page to new page
  memmove((void*)new, (void*)pa, PGSIZE);
  //decrease cnt of page reference for the old page
  PA2REFCNT(pa)--;

  release(&pgreflock);
  return (void*)new;
}