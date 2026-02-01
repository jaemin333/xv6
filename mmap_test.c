#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#ifndef PROT_READ
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#endif

#define PGSIZE 4096

char *test_str = "Hello, mmap world! This is a test.";
int test_len = 34;

// [Test 1] 기본 읽기 및 Lazy Allocation 테스트
void test_mmap_read() {
  printf(1, "[Test 1] mmap Read Test starting...\n");

  // 1. 테스트 파일 생성
  int fd = open("mmap_test.txt", O_CREATE | O_RDWR);
  if(fd < 0){
    printf(1, "Error: cannot create file\n");
    exit();
  }
  write(fd, test_str, test_len);
  close(fd);

  // 2. 파일 열기
  fd = open("mmap_test.txt", O_RDWR);
  if(fd < 0){
    printf(1, "Error: cannot open file\n");
    exit();
  }

  // 3. mmap 호출
  char *p = (char*)mmap(fd, 0, PGSIZE, PROT_READ | PROT_WRITE);
  
  if(p == 0 || p == (char*)-1){
    printf(1, "Error: mmap failed\n");
    close(fd);
    exit();
  }

  // 4. 메모리 접근
  if(p[0] == 'H' && p[7] == 'm') {
    printf(1, "  -> Memory Content: %s\n", p);
    printf(1, "[Test 1] mmap Read OK!\n");
  } else {
    printf(1, "[Test 1] mmap Read Failed! (Data mismatch)\n");
    exit();
  }

  // ★ 수정됨: (uint)p -> (void*)p
  // user.h에 정의된 munmap(void* addr, int length)에 맞춤
  if(munmap((void*)p, PGSIZE) < 0) {
      printf(1, "Error: munmap failed\n");
  }

  close(fd);
}

// [Test 2] 보호 모드 테스트
void test_mmap_protection() {
  printf(1, "\n[Test 2] mmap Protection Test (Should Die)...\n");

  int pid = fork();
  if(pid == 0) {
    int fd = open("mmap_test.txt", O_RDWR);
    if(fd < 0) exit();

    char *p = (char*)mmap(fd, 0, PGSIZE, PROT_READ);
    if(p == (char*)-1) exit();
    
    printf(1, "  -> Trying to write to Read-Only mmap area...\n");
    
    p[0] = 'X'; 
    
    printf(1, "Error: Child should have died!\n");
    exit();
  } else {
    wait();
    printf(1, "[Test 2] Child terminated (Expected).\n");
  }
}

// [Test 3] 해제 후 접근 테스트
void test_munmap_access() {
  printf(1, "\n[Test 3] munmap Access Test (Should Die)...\n");

  int fd = open("mmap_test.txt", O_RDWR);
  char *p = (char*)mmap(fd, 0, PGSIZE, PROT_READ | PROT_WRITE);
  
  // ★ 수정됨: (uint)p -> (void*)p
  if(munmap((void*)p, PGSIZE) < 0) {
      printf(1, "Error: munmap failed during setup\n");
      exit();
  }

  int pid = fork();
  if(pid == 0) {
    printf(1, "  -> Trying to access unmapped memory...\n");
    
    char c = p[0]; 
    
    printf(1, "Error: Child alive? Read '%c'. munmap failed!\n", c);
    exit();
  } else {
    wait();
    printf(1, "[Test 3] Child terminated (Expected).\n");
  }
  close(fd);
}

// [Test 4] Write-Back 테스트
void test_mmap_writeback() {
  printf(1, "\n[Test 4] mmap Write-Back Test...\n");

  int fd = open("mmap_wb.txt", O_CREATE | O_RDWR);
  write(fd, "AAAAA", 5);
  close(fd);

  fd = open("mmap_wb.txt", O_RDWR);
  char *p = (char*)mmap(fd, 0, PGSIZE, PROT_READ | PROT_WRITE);
  
  if(p == (char*)-1) {
      printf(1, "Error: mmap failed\n");
      exit();
  }

  p[0] = 'B'; 
  
  munmap((void*)p, PGSIZE); 
  close(fd);

  fd = open("mmap_wb.txt", O_RDWR);
  char buf[10];
  read(fd, buf, 5);
  close(fd);

  if(buf[0] == 'B') {
      printf(1, "  -> File content changed to 'B'. Write-Back Success!\n");
      printf(1, "[Test 4] Write-Back OK!\n");
  } else {
      printf(1, "  -> File content is '%c'. Write-Back Failed!\n", buf[0]);
      printf(1, "     (Did you implement writei in munmap?)\n");
  }
}

int main(int argc, char *argv[]) {
  test_mmap_read();
  test_mmap_protection();
  test_munmap_access();
  test_mmap_writeback();
  
  printf(1, "\n=== All mmap Tests Completed ===\n");
  exit();
}