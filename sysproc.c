#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
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

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int 
sys_nice(void)
{
  int value;

  if(argint(0,&value) < 0) return -1;

  int new_nice = setnice(myproc(),value);

  yield(); //for prioirty scheduler
  return new_nice;
}

int
sys_yield(void){
  yield();

  return 0;
}

int
sys_ps(void){
  procdump_ps();
  return 0;
}

uint
sys_mmap(void){
  int fd, offset, length, flags;
  struct file *f;

  if(argfd(0,&fd,&f) < 0) return (uint)MAP_FAILED;
  if(argint(1,&offset) < 0) return (uint)MAP_FAILED;
  if(argint(2,&length) < 0) return (uint)MAP_FAILED;
  if(argint(3,&flags) < 0) return (uint)MAP_FAILED;

  return mmap(fd,offset,length,flags,f);
}

uint
sys_munmap(void)
{
  uint addr;
  int length;

  if(argint(0,(int*)&addr) < 0) return -1;
  if(argint(1,&length) < 0) return -1;

  return munmap(addr,length);

}