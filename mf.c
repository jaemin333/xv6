#include "types.h"
#include "user.h"

#define TICK_LOOP 1000000 

int main(void) {

  printf(1, "MLFQ Scheduler Test Start (Ticks & RR Verification)\n");

  // 1. CPU-bound 프로세스 (Child 1: 강등 확인용)
  if (fork() == 0) {
    printf(1, "Child 1 (CPU-bound, PID %d) started\n", getpid());
    // 충분히 많은 양의 계산
    for (volatile int i = 0; i < 1000; i++) {
      for (volatile int j = 0; j < TICK_LOOP; j++); 
    }
    printf(1, "Child 1 finished\n");
    exit();
  }

  // 2. Interactive 프로세스 (Child 2: 우선순위 유지 확인용)
  if (fork() == 0) {
    printf(1, "Child 2 (Interactive, PID %d) started\n", getpid());
    for (int i = 0; i < 100; i++) {
      // 짧은 계산 (타임 슬라이스 미만)
      for (volatile int j = 0; j < TICK_LOOP * 10; j++); 
      
      // sleep하여 CPU 양보 (우선순위 유지 유도)
      sleep(1); 
    }
    printf(1, "Child 2 finished\n");
    exit();
  }

  
  // 3. RR 테스트용 쌍둥이 프로세스 A (Child 3)

  if (fork() == 0) {
    printf(1, "Child 3 (RR Test A, PID %d) started\n", getpid());
    // 타임 인터럽트에 의한 선점이 일어나는지 확인
    for (volatile int i = 0; i < 500; i++) { 
       for (volatile int j = 0; j < TICK_LOOP * 5; j++);
    }
    printf(1, "Child 3 (RR Test A) Finished\n");
    exit();
  }

  // 4. RR 테스트용 쌍둥이 프로세스 B (Child 4)
  if (fork() == 0) {
    printf(1, "Child 4 (RR Test B, PID %d) started\n", getpid());
    // Child 3과 완전히 동일한 작업 수행
    for (volatile int i = 0; i < 500; i++) { 
       for (volatile int j = 0; j < TICK_LOOP * 5; j++);
    }
    printf(1, "Child 4 (RR Test B) Finished\n");
    exit();
  }

  // 5. 부모 프로세스: 모니터링
  printf(1, "\n--- Observation: Initial ---\n");
  ps();

  // Child 3과 4가 경쟁하는 모습을 보기 위해 관찰 횟수 유지
  for (int i = 0; i < 15; i++) { 
    sleep(30); 
    printf(1, "\n--- Observation %d ---\n", i);
    ps();
  }

  // 자식 프로세스 4개가 모두 끝날 때까지 대기
  while (wait() != -1);
  printf(1, "MLFQ Test Complete\n"); 
  exit();
}