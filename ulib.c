#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "user.h"
#include "x86.h"

#define MAX_THREADS 64

struct{
  int tid;
  int used;
} joined_threads[MAX_THREADS];

char*
strcpy(char *s, const char *t)
{
  char *os;

  os = s;
  while((*s++ = *t++) != 0)
    ;
  return os;
}

int
strcmp(const char *p, const char *q)
{
  while(*p && *p == *q)
    p++, q++;
  return (uchar)*p - (uchar)*q;
}

uint
strlen(const char *s)
{
  int n;

  for(n = 0; s[n]; n++)
    ;
  return n;
}

void*
memset(void *dst, int c, uint n)
{
  stosb(dst, c, n);
  return dst;
}

char*
strchr(const char *s, char c)
{
  for(; *s; s++)
    if(*s == c)
      return (char*)s;
  return 0;
}

char*
gets(char *buf, int max)
{
  int i, cc;
  char c;

  for(i=0; i+1 < max; ){
    cc = read(0, &c, 1);
    if(cc < 1)
      break;
    buf[i++] = c;
    if(c == '\n' || c == '\r')
      break;
  }
  buf[i] = '\0';
  return buf;
}

int
stat(const char *n, struct stat *st)
{
  int fd;
  int r;

  fd = open(n, O_RDONLY);
  if(fd < 0)
    return -1;
  r = fstat(fd, st);
  close(fd);
  return r;
}

int
atoi(const char *s)
{
  int n;

  n = 0;
  while('0' <= *s && *s <= '9')
    n = n*10 + *s++ - '0';
  return n;
}

void*
memmove(void *vdst, const void *vsrc, int n)
{
  char *dst;
  const char *src;

  dst = vdst;
  src = vsrc;
  while(n-- > 0)
    *dst++ = *src++;
  return vdst;
}

int thread_create(void (*func)(void *), void *arg)
{
  void *orig_stack = malloc(4096*2);
  void *aligned_stack;

  if(orig_stack == 0){
    printf(1, "DEBUG: malloc failed!\n"); // <--- 추가
    return -1;
  }

  uint addr = (uint)orig_stack;

  if(addr % 4096 !=0){
    addr = (addr+4096) & ~0xFFF;
  }
  aligned_stack = (void*)addr;

  int tid = clone(aligned_stack);

  if(tid < 0){
    printf(1, "DEBUG: clone syscall failed! stack=%p\n", aligned_stack); // <--- 추가
    free(orig_stack);
    return -1;
  }

  // only child thread execute fuc
  if(tid == 0){
    func(arg);

    free(orig_stack);

    exit();
  }

  return tid;
}

int
thread_join(int tid)
{
  int ret_tid;
  int i;

  for(i=0; i<MAX_THREADS; i++){
    if(joined_threads[i].used && joined_threads[i].tid == tid){
      joined_threads[i].used = 0;
      return 0;
    }
  }

  while(1){
    ret_tid = join();

    if(ret_tid == -1){
      return -1;
    }

    if(ret_tid == tid){
      return 0;
    }

    for(i=0; i<MAX_THREADS; i++){
      if(!joined_threads[i].used){
        joined_threads[i].tid = ret_tid;
        joined_threads[i].used = 1;
        break;
      }
    }
  }
}