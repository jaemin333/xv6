#include "types.h"
#include "user.h"

#define CPU_WORK 10000000

// 단순 계산을 수행하는 함수 (CPU-bound)
void do_cpu_work() {
  for (volatile int i = 0; i < CPU_WORK; i++) {
    // 계산 수행
  }
}

int main(void) {
  int pid;

  printf(1, "MLFQ Scheduler Test Start\n");

  // 1. CPU-bound 프로세스 생성 (Queue 0 -> 1 -> 2 강등 예상)
  pid = fork();
  if (pid == 0) {
    printf(1, "Child 1 (CPU-bound, PID %d) started\n", getpid());
    for (int i = 0; i < 10; i++) {
      do_cpu_work();
      // 계산 중간중간 상태 확인을 위해 ps() 호출 가능
    }
    printf(1, "Child 1 finished\n");
    exit();
  }

  // 2. Interactive 프로세스 생성 (Queue 0 유지 예상)
  pid = fork();
  if (pid == 0) {
    printf(1, "Child 2 (Interactive, PID %d) started\n", getpid());
    for (int i = 0; i < 50; i++) {
      // 짧게 계산하고 sleep()으로 CPU 양보
      for(volatile int j=0; j<100000; j++); 
      sleep(2); // Rule 4: 타임 슬라이스 소진 전 양보하므로 우선순위 유지
    }
    printf(1, "Child 2 finished\n");
    exit();
  }

  // 3. 부모 프로세스는 주기적으로 상황을 모니터링
  for (int i = 0; i < 10; i++) {
    sleep(15);
    printf(1, "\n--- Observation %d ---\n", i);
    ps(); // 이전에 만든 ps 시스템 콜 활용
  }

  while (wait() != -1);
  printf(1, "MLFQ Test Complete\n");
  exit();
}