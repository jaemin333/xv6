#include "types.h"
#include "user.h"

// 환경에 따라 1틱당 루프 횟수를 조절하세요. 
// 보통 100만 루프가 1틱 내외입니다.
#define TICK_LOOP 1000000 

int main(void) {

  printf(1, "MLFQ Scheduler Test Start (Ticks Verification)\n");

  // 1. CPU-bound 프로세스 (Child 1)
  if (fork() == 0) {
    printf(1, "Child 1 (CPU-bound, PID %d) started\n", getpid());
    // 충분히 많은 양의 계산 (강등 확인용)
    for (volatile int i = 0; i < 1000000; i++) {
      for (volatile int j = 0; j < TICK_LOOP; j++); 
    }
    exit();
  }

  // 2. Interactive 프로세스 (Child 2)
  if (fork() == 0) {
    printf(1, "Child 2 (Interactive, PID %d) started\n", getpid());
    for (int i = 0; i < 100; i++) {
      // [수정] TICK_LOOP * 3 정도로 설정하여 확실히 2~3틱을 소모하게 함
      // 타임 슬라이스인 4틱을 넘지 않으므로 Prior 0을 유지해야 합니다.
      for (volatile int j = 0; j < TICK_LOOP * 10; j++); 
      
      // 계산 후 짧게 sleep하여 CPU 양보 및 타임 슬라이스 초기화
      sleep(1); 
    }
    printf(1, "Child 2 finished\n");
    exit();
  }

  // 3. 부모 프로세스: 모니터링
  printf(1, "\n--- Observation: Initial ---\n");
  ps();

  for (int i = 0; i < 10; i++) {
    sleep(30); // 관찰 간격을 조금 더 넉넉히 둡니다.
    printf(1, "\n--- Observation %d ---\n", i);
    ps();
  }

  while (wait() != -1);
  printf(1, "MLFQ Test Complete\n");
  exit();
}