#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

//lab5a
uint64
sys_sbrk(void)
{
  int addr;
  int n;
  struct proc *p = myproc();
  if(argint(0, &n) < 0)
    return -1;
  //addr = myproc()->sz;
  //if(growproc(n) < 0)
    //return -1;
  addr = p->sz; //save old size
  //if we want process to shrink, deallocate
  if(n < 0){
    uvmdealloc(p->pagetable, p->sz, p->sz+n);
  }
  myproc()->sz += n;  //increse process size
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  // lab3-3
  //define the three parameter
  uint64 addr;
  int numpages;
  int bitmask;

  //read the parameters from user space
  if(argaddr(0, &addr) < 0)
    return -1;
  if(argint(1, &numpages) < 0)
    return -1;
  if(argint(2, &bitmask) < 0)
    return -1;

  //set a upper bound for number of pages to test
  if(numpages < 0 || numpages > 32){
    return -1;
  }

  struct proc *p = myproc();
  int ans = 0x0;

  for(int i = 0; i < 32; i++){
    int va = addr + i * PGSIZE; //next page to check
    if(accessed_page(p->pagetable, va)){
      ans |= (1 << i); //if the page is accessed, set the ith bit in bitmask
    }
  }

  //copy bitmask to user space
  if(copyout(p->pagetable, bitmask, (char*) &ans, sizeof(ans)) < 0){
    return -1;
  }

  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
