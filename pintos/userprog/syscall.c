#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include <stddef.h>
#include "console.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
static int64_t get_user (const uint8_t *uaddr) {
    int64_t result;
    __asm __volatile (
    "movabsq $done_get, %0\n"
    "movzbq %1, %0\n"
    "done_get:\n"
    : "=&a" (result) : "m" (*uaddr));
    return result;
}

static bool put_user (uint8_t *udst, uint8_t byte) {
    int64_t error_code;
    __asm __volatile (
    "movabsq $done_put, %0\n"
    "movb %b2, %1\n"
    "done_put:\n"
    : "=&a" (error_code), "=m" (*udst) : "q" (byte));
    return error_code != -1;
}
/* 시스템 콜

 * 과거에는 시스템 콜 서비스를 인터럽트 핸들러가 처리했습니다
 * (예: 리눅스에서 `int 0x80`).

 * 그러나 x86-64 아키텍처에서는 CPU 제조사가 시스템 콜을 요청하기 위한
 * 더 효율적인 경로인 `syscall` 명령어를 제공합니다.

 * `syscall` 명령어는 Model Specific Register(MSR)의 값을 참조하여 동작합니다.
 * 자세한 내용은 CPU 매뉴얼을 참고하세요.
 */
#define MSR_STAR 0xc0000081         /* 세그먼트 선택자 MSR (Model Specific Register) */
#define MSR_LSTAR 0xc0000082        /* 롱 모드(Long mode)에서 SYSCALL 명령이 점프할 대상 주소 */
#define MSR_SYSCALL_MASK 0xc0000084 /* EFLAGS 레지스터에 적용할 마스크 */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

/* 인터럽트 서비스 루틴(ISR)은,
 * syscall_entry가 사용자 스택을 커널 모드 스택으로 바꿀 때까지는
 * 어떤 인터럽트도 처리하면 안 됩니다.
 * 따라서 우리는 FLAG_FL (Interrupt Flag)을 마스킹했습니다.
 */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* 주요 시스템 콜 인터페이스 */
void
syscall_handler (struct intr_frame *f UNUSED) {
	switch (f->R.rax) {
		case SYS_EXIT:
			{
				// int status = (int)f->R.rdi;
				// // exit status 출력 - 이것이 중요!
				// printf("%s: exit(%d)\n", thread_current()->name, status);
				// thread_exit();
				int status = (int)f->R.rdi;
		        printf("%s: exit(%d)\n", thread_current()->name, status);

		        // 실험을 위해 죽이지 않도록 주석 처리
		        // thread_exit();

		        // 대신 임시로 무한 루프 대체 (디버깅 목적)
		        while (true)
			    thread_yield();  // CPU 양보하며 계속 실행
		         break;
				
			}
			break;
			
		case SYS_WRITE:
			{
				int fd = (int)f->R.rdi; //파일 열기를 레지스터에서 찾는다.
				const void *buffer = (const void *)f->R.rsi; //파일의 이름을 레지스터에서 찾는다.
				unsigned size = (unsigned)f->R.rdx; 
				if (fd == 1) {  // stdout
					char get_buffer[128];

					if(size > sizeof(get_buffer)) size = sizeof(get_buffer);

					for(unsigned i = 0; i < size; i++)
					{
						int val = get_user((uint8_t *)buffer + i);
						if (val == -1){
							f->R.rax = -1;
							return;
						}
						get_buffer[i] = (char)val;
					}
					/* 안전 복사된 버퍼 출력 */
					putbuf(get_buffer, size);
					f->R.rax = size;
				} else {
					f->R.rax = -1;
				}
			}
			break;
			
		default:
			printf("Unimplemented system call: %lld\n", f->R.rax);
			thread_exit();
			break;
	}
}
