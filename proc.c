#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"

#ifndef PTE_D
#define PTE_D 0x040
#endif

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

struct spinlock mmap_lock;
int global_mmap_count = 0;

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->nice = 2;
  p->ticks = 0;
  p->priority = 0;
  p->time_slice = 4;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  cprintf("%p %p\n", _binary_initcode_start, _binary_initcode_size);
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  //PA4 
  if(curproc->is_thread == 1) return -1;

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }
  
  // child process inherits the nice value of the parent process
  np->nice = curproc->nice;


  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  //PA4
  np->tid = np->pid;
  np->is_thread = 0;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

int
clone(void *stack)
{
  struct proc *np;
  struct proc *curproc = myproc();

  if((uint)stack % 4096 !=0) return -1;

  if((np = allocproc()) == 0 ) return -1;

  np->pgdir = curproc->pgdir;
  np->sz = curproc->sz;
  *np->tf = *curproc->tf;

  //stack allocation, copy the caller's stack to the new thread's stack
  uint user_src_stack = (curproc->tf->esp) & ~0XFFF;
  uint offset = (uint)stack - user_src_stack;

  memmove(stack,(void*)user_src_stack,4096);
  np->tf->esp = curproc->tf->esp + offset;
  np->tf->ebp = curproc->tf->ebp + offset;

  for(int i=0; i< NOFILE; i++){
    if(curproc->ofile[i]){
      np->ofile[i] = filedup(curproc->ofile[i]);
    }
  }

  np->cwd = idup(curproc->cwd);
  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  np->tid = np->pid;
  np->pid = curproc->pid;

  np->is_thread = 1;
  np->parent = curproc;

  np->tf->eax = 0;

  acquire(&ptable.lock);
  np->state = RUNNABLE;
  release(&ptable.lock);

  return np->tid;

}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p = myproc();
  int fd;

  if(curproc == initproc)
    panic("init exiting");


  if(curproc->is_thread == 0){
    for(int i=0; i<4; i++){
      if(p->mmaps[i].used){
        munmap(p->mmaps[i].addr, p->mmaps[i].length);
      }
    }

    // Close all open files.
    for(fd = 0; fd < NOFILE; fd++){
      if(curproc->ofile[fd]){
        fileclose(curproc->ofile[fd]);
        curproc->ofile[fd] = 0;
      }
    }

    begin_op();
    iput(curproc->cwd);
    end_op();
    curproc->cwd = 0;
  }

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        if(p->is_thread == 0){
          freevm(p->pgdir);
        }
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

int
join(void)
{
  struct proc *p;
  int havekids, pid;

  struct proc *curproc = myproc();

  if(curproc->is_thread == 1) return -1;

  acquire(&ptable.lock);
  for(;;){
    havekids = 0;
    for(p = ptable.proc; p<&ptable.proc[NPROC]; p++){
      if(p->parent != curproc || p->is_thread != 1) continue;
      havekids = 1;

      if(p->state == ZOMBIE){
        pid = p->tid;
        kfree(p->kstack);
        p->kstack = 0;

        p->pgdir = 0; //different with wait
        p->pid = 0;
        p->parent = 0;
        p->killed = 0;
        p->state = UNUSED;
        
        release(&ptable.lock);
        return pid;
      }
    }

    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    sleep(curproc,&ptable.lock);
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.

/*
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}
*/


/*
// Objective 1: Priority Scheduler
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  for(;;){
    sti();
    acquire(&ptable.lock);

    struct proc *highest_p = 0;

    for(p = ptable.proc; p<&ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE) continue;
      if(highest_p == 0){
        highest_p = p;
        continue;
      }
      if(p->nice < highest_p->nice){
        highest_p = p;
      }
      else if(p->nice == highest_p->nice && p->pid < highest_p->pid){
        highest_p = p;
      }
    }

    if(highest_p != 0){
      p = highest_p;
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      c->proc = 0;
    }
    release(&ptable.lock);
  }
}
*/


//Objective 2: MLFQ Scheduler
void
scheduler(void){
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;

  for(;;){
    sti();
    acquire(&ptable.lock);


    for(int q=0; q<3; q++){
      int found_proc_in_this_queue = 0;
      for(p=ptable.proc; p<&ptable.proc[NPROC]; p++){
        if(p->state != RUNNABLE || p->priority != q) continue;

        c->proc = p;
        switchuvm(p);
        p->state = RUNNING;
        swtch(&(c->scheduler), p->context);
        switchkvm();
        c->proc = 0;
        found_proc_in_this_queue = 1;
      }
      if(found_proc_in_this_queue){
        q = -1;
      }
    }
    release(&ptable.lock);
  }

}



// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

int
setnice(struct proc *p,int value){
  acquire(&ptable.lock);

  p->nice +=value;

  if(p->nice <-5) p->nice = -5;
  if(p->nice > 4) p->nice = 4;

  release(&ptable.lock);

  return p->nice;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;

  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  
  p->time_slice = 4;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

void
procdump_ps(void){
  struct proc *p;
  extern uint ticks;

  cprintf("name\tpid\tstate\tprior\tticks: %d\n",ticks);

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == SLEEPING || p->state == RUNNABLE || p->state == RUNNING || p->state == ZOMBIE){
      cprintf("%s\t%d\t%d\t%d\t%d\n", p->name, p->pid, p->state, p->priority, p->ticks);
    }
  }

  release(&ptable.lock);
}


uint
mmap(int fd, int offset, int length, int flags, struct file *f)
{
  struct proc *p = myproc();
  uint start_addr;
  int slot = -1;

  if((flags & MAP_PROT_WRITE) && (f->writable == 0)){
    return (uint)MAP_FAILED;
  }
  if(offset % PGSIZE != 0) return (uint)MAP_FAILED;
  if(length <= 0) return (uint)MAP_FAILED;

  acquire(&mmap_lock);
  if(global_mmap_count >= 16){
    release(&mmap_lock);
    return (uint)MAP_FAILED;
  }
  global_mmap_count++;
  release(&mmap_lock);

  for(int i=0; i<4; i++){
    if(p->mmaps[i].used == 0){
      slot = i;
      break;
    }
  }

  if(slot == -1){
    acquire(&mmap_lock);
    global_mmap_count--;
    release(&mmap_lock);
    return (uint)MAP_FAILED;
  }

  // KERNBASE의 반부터 시작
  start_addr = 0x40000000 + (slot * 0x100000);

  struct mmap_page *m = &p->mmaps[slot];
  m->used = 1;
  m->addr = start_addr;
  m->length = length;
  m->offset = offset;
  m->fd = fd;
  m->prot = flags;
  m->f = filedup(f);

  /*
  //objective 2
  int bytes_left = length;
  int curr_off = offset;
  uint curr_addr = start_addr;

  while(bytes_left > 0){
    char *mem = kalloc();
    if(mem == 0) goto bad;
    memset(mem,0,PGSIZE);

    int n = (bytes_left < PGSIZE) ? bytes_left : PGSIZE;

    ilock(f->ip);
    if(readi(f->ip,(char*)mem, curr_off,n) < 0){
      iunlock(f->ip);
      kfree(mem);
      goto bad;
    }
    iunlock(f->ip);

    int perm = PTE_U | PTE_P;
    if(flags & MAP_PROT_WRITE) perm |= PTE_W;

    if(mappages(p->pgdir, (void*)curr_addr, PGSIZE, V2P(mem),perm) <0){
      kfree(mem);
      goto bad;
    }
    
    bytes_left -= PGSIZE;
    curr_off += PGSIZE;
    curr_addr += PGSIZE;
  }
  */
  

  return start_addr;

  /*
  bad:
    fileclose(m->f);
    m->used = 0;
    acquire(&mmap_lock);
    global_mmap_count--;
    release(&mmap_lock);
    return (uint)MAP_FAILED;
  */
}

uint
munmap(uint addr, int length)
{
  struct proc *p = myproc();
  struct mmap_page *m = 0;

  if(addr % PGSIZE != 0) return -1;

  for(int i=0; i<4; i++){
    if(p->mmaps[i].used && p->mmaps[i].addr == addr){
      m = &p->mmaps[i];
      break;
    }
  }

  if(m == 0) return 0;

  if(m->length != length) return -1;

  uint curr_addr = addr;
  int bytes_left = length;

  while(bytes_left > 0){
    pte_t *pte = walkpgdir(p->pgdir, (void*)curr_addr,0);

    if(pte && (*pte & PTE_P)){
      uint pa = PTE_ADDR(*pte);

      if(*pte & PTE_D){
        int n = (bytes_left < PGSIZE) ? bytes_left : PGSIZE;
        int file_off = (curr_addr - m->addr) + m->offset;
        
        begin_op();
        ilock(m->f->ip);
        writei(m->f->ip, (char*)P2V(pa), file_off,n);
        iunlock(m->f->ip);
        end_op();
      }

      if(pa != 0) kfree((char*)P2V(pa));

      *pte = 0;
    }

    curr_addr += PGSIZE;
    bytes_left -= PGSIZE;
  }

  fileclose(m->f);
  m->used = 0;
  m->f = 0;

  acquire(&mmap_lock);
  global_mmap_count--;
  release(&mmap_lock);

  return 0;

}

int mutex_lock(void* l)
{
  acquire(&ptable.lock);

  while(*(int*)l != 0){
    sleep(l,&ptable.lock);
  }

  *(int*)l = 1;

  release(&ptable.lock);

  return 0;
}

int
mutex_unlock(void *l)
{
  acquire(&ptable.lock);

  *(int*)l = 0;
  wakeup1(l);

  release(&ptable.lock);

  return 0;
}