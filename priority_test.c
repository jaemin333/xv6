#include "types.h"
#include "user.h"

// CPU 점유를 위한 연산 함수
void do_heavy_work(int id) {
  volatile int i, j;
  // 10000 -> 200000으로 대폭 수정
  for (i = 0; i < 200000; i++) { 
    for (j = 0; j < 5000; j++) {
       // CPU를 계속 쓰게 만듦
    }
  }
  printf(1, "Child %d (PID %d) finished!\n", id, getpid());
}

int main(void) {
  int pid;
  int priorities[3] = {-5, 0, 4}; // 최고, 중간, 최저

  printf(1, "Priority Scheduler Test Start\n");

  for (int i = 0; i < 3; i++) {
    pid = fork();
    if (pid == 0) {
      // 자식 프로세스: 우선순위 설정 후 연산 시작
      nice(priorities[i]);
      printf(1, "Child %d (PID %d) started with nice %d\n", i, getpid(), priorities[i]);
      
      do_heavy_work(i);
      exit();
    }
  }

  // 부모 프로세스: 자식들이 종료되는 동안 상황 모니터링
  for (int i = 0; i < 5; i++) {
    sleep(20);
    printf(1, "\n--- Observation %d ---\n", i);
    ps();
  }

  // 모든 자식 대기
  while (wait() != -1);
  printf(1, "Priority Test Complete\n");
  exit();
}