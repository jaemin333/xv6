#include "types.h"
#include "stat.h"
#include "user.h"

#define PGSIZE 4096

// 1. 스택 성장 테스트
void test_stack_growth(int depth) {
  char buf[1024];
  
  // 500번마다 현재 스택 주소를 출력
  if (depth % 500 == 0) {
      printf(1, "Depth %d: stack addr 0x%x\n", depth, buf);
  }

  for(int i = 0; i < 1024; i++) {
    buf[i] = 'A'; 
  }

  if (depth > 0) {
    test_stack_growth(depth - 1);
    
    // 컴파일러 속이기용 (printf가 buf를 썼으므로 최적화 못함)
    if(buf[0] == 'C') printf(1, "."); 
  }
}

// 2. 힙(Heap) 게으른 할당 테스트
void test_heap_lazy() {
  printf(1, "[Test 2] Heap Lazy Allocation test starting...\n");
  
  int size = 10 * PGSIZE; // 40KB 할당 요청
  char *p = sbrk(size);
  
  if (p == (char*)-1) {
    printf(1, "sbrk failed\n");
    exit();
  }

  // 실제 할당은 여기서(Write 시점) 일어나야 함
  printf(1, "  -> Accessing Heap memory...\n");
  p[0] = 'a';              // 첫 페이지 할당
  p[size - 1] = 'z';       // 마지막 페이지 할당

  if (p[0] == 'a' && p[size - 1] == 'z') {
    printf(1, "[Test 2] Heap Lazy Allocation OK!\n");
  } else {
    printf(1, "[Test 2] Heap Lazy Allocation Failed!\n");
  }
}

// 3. 스택 가드(Overflow) 테스트
void test_stack_guard() {
  printf(1, "[Test 3] Stack Guard test (Should Die)...\n");

  int pid = fork();
  if (pid == 0) {
    // 자식 프로세스
    // 5페이지(20KB) 짜리 배열을 선언 -> 16KB 제한 돌파 시도
    char big_stack[5 * PGSIZE]; 
    
    // 가드 페이지를 건드리는 순간 죽어야 함
    for(int i = 0; i < 5 * PGSIZE; i++) {
        big_stack[i] = 'X'; 
    }
    
    // [수정] 컴파일러 에러 방지용 (여기 도달하기 전에 죽겠지만, 컴파일러를 속이기 위해 추가)
    if (big_stack[0] == 'Y') {
        printf(1, "This will never happen\n");
    }
    
    printf(1, "ERROR: Child should have died by now!\n");
    exit();
  } else {
    // 부모 프로세스
    wait(); // 자식이 죽을 때까지 대기
    printf(1, "[Test 3] Child process terminated (Expected).\n");
  }
}

int main(int argc, char *argv[]) {
  printf(1, "=== Demand Paging Test Start ===\n\n");

  // 1. Stack Growth Test
  printf(1, "[Test 1] Stack Growth test starting...\n");
  // 깊이 12 * 1KB = 12KB 사용 (16KB 제한 안쪽)
  test_stack_growth(14); 
  printf(1, "[Test 1] Stack Growth OK!\n\n");

  // 2. Heap Test
  test_heap_lazy();
  printf(1, "\n");

  // 3. Guard Page Test
  test_stack_guard();

  printf(1, "\n=== All Tests Completed ===\n");
  exit(); 
}