#include "types.h"
#include "user.h"

// CPU를 점유하기 위한 무거운 계산 함수
void do_work() {
  volatile int i, j;
  for (i = 0; i < 100000; i++) {
    for (j = 0; j < 1000; j++) {
      // 아무것도 하지 않고 CPU만 사용함
    }
  }
}

int main() {
  int pid1, pid2;

  printf(1, "Starting test... Parent (pid %d) has nice 2\n", getpid());

  pid1 = fork();
  if (pid1 == 0) {
    // 첫 번째 자식: 우선순위를 아주 낮게 설정 (nice 20)
    nice(20); 
    printf(1, "Child 1 (pid %d) starts with nice 20\n", getpid());
    for (int k = 0; k < 50; k++) {
      do_work();
    }
    printf(1, "Child 1 finished!\n");
    exit();
  }

  pid2 = fork();
  if (pid2 == 0) {
    // 두 번째 자식: 우선순위를 보통으로 설정 (nice 5)
    nice(5);
    printf(1, "Child 2 (pid %d) starts with nice 5\n", getpid());
    for (int k = 0; k < 50; k++) {
      do_work();
    }
    printf(1, "Child 2 finished!\n");
    exit();
  }

  // 부모 프로세스가 자식들이 일하는 동안 ps를 주기적으로 호출
  for (int m = 0; m < 5; m++) {
    sleep(20); // 자식들이 CPU를 점유할 시간을 줌
    printf(1, "\n--- Observation %d ---\n", m);
    ps(); // 시스템 콜 호출
  }

  wait();
  wait();
  printf(1, "Test complete.\n");
  exit();
}