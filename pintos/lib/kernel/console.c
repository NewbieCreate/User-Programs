#include <console.h>
#include <stdarg.h>
#include <stdio.h>
#include "devices/serial.h"
#include "devices/vga.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/synch.h"

static void vprintf_helper (char, void *);
static void putchar_have_lock (uint8_t c);

/* 콘솔 락(console lock)

   VGA 계층과 시리얼(serial) 계층은 각각 자체적인 락을 가지고 있기 때문에,
   언제든지 그들을 호출해도 안전합니다.

   하지만 이 콘솔 락은 동시에 여러 printf() 호출이 일어나서
   출력 내용이 섞이는 것을 방지하는 데 유용합니다.
   출력이 섞이면 매우 혼란스러워 보이기 때문입니다.
 */
static struct lock console_lock;

/* 일반적인 상황에서는 true입니다.
   위에서 설명한 것처럼, 여러 스레드 간 출력이 섞이는 것을 방지하기 위해 console_lock을 사용하려고 하기 때문입니다.

   하지만 다음과 같은 예외 상황에서는 false가 됩니다:
   - 부트 초기 단계에서 아직 락(lock) 시스템이 동작하지 않거나 console_lock이 초기화되기 전
   - 또는 커널 패닉 이후

   첫 번째 경우에는 락을 시도하면 assertion failure(단정문 오류)가 발생하게 되고,
   이는 커널 패닉으로 이어져 결국 두 번째 경우가 됩니다.

   두 번째 경우(커널 패닉 이후)에는 만약 panic을 유발한 원인이 buggy한 lock_acquire() 구현이었다면,
   다시 락을 시도하면서 재귀적으로 같은 문제가 반복될 수 있습니다.
 */
static bool use_console_lock;

/* 디버그 출력을 충분히 추가하면,
   Pintos에서 하나의 스레드가 console_lock을 재귀적으로 획득하려고 시도하는 상황이 발생할 수 있습니다.

   실제 예를 하나 들자면, 제가 palloc_free() 함수 안에 printf() 호출을 추가했을 때 발생한 일입니다.
   다음은 그 결과로 발생한 실제 백트레이스입니다:

   lock_console()
   vprintf()
   printf()             - 여기서 palloc()이 다시 lock을 잡으려 함
   palloc_free()        
   schedule_tail()      - 다른 스레드가 죽고 해당 스레드로 전환 중
   schedule()
   thread_yield()
   intr_handler()       - 타이머 인터럽트 발생
   intr_set_level()
   serial_putc()
   putchar_have_lock()
   putbuf()
   sys_write()          - 한 프로세스가 콘솔에 쓰기 요청
   syscall_handler()
   intr_handler()

   이런 문제는 디버깅이 매우 어렵기 때문에,
   우리는 락을 재귀적으로 획득하는 것처럼 동작하게 하기 위해,
   'depth counter(깊이 카운터)'를 사용해 이 문제를 방지합니다.
 */
static int console_lock_depth;

/* Number of characters written to console. */
static int64_t write_cnt;

/* 콘솔 출력을 보호하기 위해 락 기능을 활성화합니다. */
void
console_init (void) {
	lock_init (&console_lock);
	use_console_lock = true;
}

/* Notifies the console that a kernel panic is underway,
   which warns it to avoid trying to take the console lock from
   now on. */
void
console_panic (void) {
	use_console_lock = false;
}

/* Prints console statistics. */
void
console_print_stats (void) {
	printf ("Console: %lld characters output\n", write_cnt);
}

/* Acquires the console lock. */
	static void
acquire_console (void) {
	if (!intr_context () && use_console_lock) {
		if (lock_held_by_current_thread (&console_lock)) 
			console_lock_depth++; 
		else
			lock_acquire (&console_lock); 
	}
}

/* Releases the console lock. */
static void
release_console (void) {
	if (!intr_context () && use_console_lock) {
		if (console_lock_depth > 0)
			console_lock_depth--;
		else
			lock_release (&console_lock); 
	}
}

/* Returns true if the current thread has the console lock,
   false otherwise. */
static bool
console_locked_by_current_thread (void) {
	return (intr_context ()
			|| !use_console_lock
			|| lock_held_by_current_thread (&console_lock));
}

/* 표준 vprintf() 함수입니다.
   이 함수는 printf()와 유사하지만, 인자 전달에 va_list를 사용합니다.
   출력은 VGA 디스플레이와 시리얼 포트 양쪽 모두에 출력됩니다.
 */
int
vprintf (const char *format, va_list args) {
	int char_cnt = 0;

	acquire_console ();
	__vprintf (format, args, vprintf_helper, &char_cnt);
	release_console ();

	return char_cnt;
}

/* 문자열 S를 콘솔에 출력하고, 그 뒤에 줄 바꿈(new-line) 문자를 추가합니다. */
int
puts (const char *s) {
	acquire_console ();
	while (*s != '\0')
		putchar_have_lock (*s++);
	putchar_have_lock ('\n');
	release_console ();

	return 0;
}

/* BUFFER에 있는 N개의 문자를 콘솔에 출력합니다. */
void
putbuf (const char *buffer, size_t n) {
	acquire_console ();
	while (n-- > 0)
		putchar_have_lock (*buffer++);
	release_console ();
}

/* Writes C to the vga display and serial port. */
int
putchar (int c) {
	acquire_console ();
	putchar_have_lock (c);
	release_console ();

	return c;
}

/* Helper function for vprintf(). */
static void
vprintf_helper (char c, void *char_cnt_) {
	int *char_cnt = char_cnt_;
	(*char_cnt)++;
	putchar_have_lock (c);
}

/* Writes C to the vga display and serial port.
   The caller has already acquired the console lock if
   appropriate. */
static void
putchar_have_lock (uint8_t c) {
	ASSERT (console_locked_by_current_thread ());
	write_cnt++;
	serial_putc (c);
	vga_putc (c);
}
