#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "fcntl.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }
  else if(tf->trapno == T_PGFLT){
    struct proc *p = myproc();
    uint va = rcr2(); // cr2 register contains a faulted virtual address

    if(va >= KERNBASE){
      p->killed = 1;
      return;
    }

    struct mmap_page *m = 0;
    for(int i=0; i<4; i++){
      if(p->mmaps[i].used && va >= p->mmaps[i].addr && va < p->mmaps[i].addr + p->mmaps[i].length){
        m = &p->mmaps[i];
        break;
      }
    }

    if(m){
      if((tf->err & 2) && !(m->prot & MAP_PROT_WRITE)){
        cprintf("mmap write protection\n");
        p->killed = 1;
        return;
      }

      char *mem = kalloc();
      if(mem == 0){
        p->killed = 1;
        return;
      }
      memset(mem,0,PGSIZE);

      uint va_start = PGROUNDDOWN(va);
      int offset = (va_start - m->addr) + m->offset;
      int n = PGSIZE;
      if(va_start + n > m->addr + m->length) n = (m->addr + m->length) - va_start;

      ilock(m->f->ip);
      readi(m->f->ip, mem, offset, n); 
      iunlock(m->f->ip);

      int perm = PTE_U | PTE_P;
      if(m->prot & MAP_PROT_WRITE) perm |= PTE_W;

      if(mappages(p->pgdir, (void*)va_start, PGSIZE, V2P(mem), perm) < 0){
        kfree(mem);
        p->killed = 1;
      }
      return; 
    }

    if(tf->err &1){ //p bit t-err는 페이지는 있지만 권한문제
      p->killed = 1;
      return;
    }


    if(va < p->sz){
      char *mem = kalloc();
      if(mem ==0){
        p->killed = 1;
        return;
      }
      memset(mem,0,PGSIZE);

      if(mappages(p->pgdir, (void*)PGROUNDDOWN(va), PGSIZE, V2P(mem), PTE_W|PTE_U|PTE_P) < 0){
        kfree(mem);
        p->killed = 1;
      }

      return;
    }


  }

  switch(tf->trapno){
  // kernel/trap.c
  case T_IRQ0 + IRQ_TIMER:
    lapiceoi();
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }

    if(myproc() && myproc()->state == RUNNING){
      myproc()->ticks++; 
      myproc()->time_slice--;
      // 1. 타임 슬라이스를 다 썼다면 강등시키고 양보
      if(myproc()->time_slice <= 0){
        if(myproc()->priority < 2){
          myproc()->priority++;
        }  
        myproc()->time_slice = 4; // 초기화
        yield();
      } 
      // 2. 슬라이스가 남았더라도 매 틱마다 무조건 yield를 시도
      else {
        yield();
        //in priority scheduler, dont use yield
      }
    }
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
//  case T_IRQ0 + IRQ_IDE2:
//	ide2intr();
//	lpaiceoi();
//	break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
