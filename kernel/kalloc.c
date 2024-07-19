// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU]; //lab-locks: let each CPU has its own free list of physical pages

char *kmemlock_names[] = {
  "kmem0",
  "kmem1",
  "kmem2",
  "kmem3",
  "kmem4",
  "kmem5",
  "kmem6",
  "kmem7"
};

void
kinit()
{
  //initialize kmem for each cpu
  for(int i = 0; i < NCPU; i++){
    initlock(&kmem[i].lock, kmemlock_names[i]);
  }
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
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);
  r = (struct run*)pa;

  //lab-locks
  push_off(); //disable interrupts so that we can call cpuid()
  int cpu = cpuid();
  acquire(&kmem[cpu].lock);
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
  release(&kmem[cpu].lock);
  pop_off(); //enable interrupt
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  //lab-locks
  push_off(); //disable interrupts so that we can call cpuid()
  int cpu = cpuid();
  acquire(&kmem[cpu].lock);

  if(!kmem[cpu].freelist){  //if freelist for current CPU is empty
    steal_pages(cpu); //steal 16 pages from other CPU
  }

  r = kmem[cpu].freelist;
  if(r)
    kmem[cpu].freelist = r->next;
  release(&kmem[cpu].lock);
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

void
steal_pages(int cpu){
  int stolen = 0; //number of pages to steal
  for(int i = 0; i < NCPU; i++){
    if(i == cpu) continue;
    if(stolen == 16) break; //we want to steal 16 pages
    acquire(&kmem[i].lock);
    struct run *rs = kmem[i].freelist;
    //now steal a page
    while(rs && stolen <= 16){  //if there are still free pages and we didn't steal enough(16 pages)
      kmem[i].freelist = rs->next;  //move head of the src list to next
      rs->next = kmem[cpu].freelist;  //appead the page stolen to head of des cpu's freelist
      kmem[cpu].freelist = rs;  //update head of des cpu's free list
      rs = kmem[i].freelist;  //move rs to the next empty page in src cpu's freelist
      stolen++; //now we finished stealing 1 page
    }
    release(&kmem[i].lock);
  }
}
