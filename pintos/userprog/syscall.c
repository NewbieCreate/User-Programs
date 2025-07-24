#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

int64_t
get_user (const uint8_t *uaddr) {
    int64_t result;

    asm volatile (
        "1:\n"
        "movzbq (%1), %0\n"     // uaddr의 1바이트를 읽어서 zero-extend하여 result에 저장
        "2:\n"
        ".pushsection .text\n"
        ".global user_access_end_get\n"
        "user_access_end_get:\n"
        "ret\n"
        ".popsection\n"
        ".section .fixup,\"ax\"\n"
        "3:\n"
        "mov $-1, %0\n"         // 예외 발생 시 -1 리턴
        "jmp 2b\n"              // 다시 정상 흐름으로 점프
        ".previous\n"
        ".section __ex_table,\"a\"\n"
        ".quad 1b,3b\n"         // 예외 복구 테이블: 1번 줄 예외 발생 시 3번 줄로 점프
        ".previous"
        : "=&a" (result)
        : "r" (uaddr)
        : "memory"
    );

    return result;
}


bool
put_user (uint8_t *udst, uint8_t byte) {
    int success;

    asm volatile (
        "1:\n"
        "movb %b2, (%1)\n"
        "mov $0, %0\n"
        "2:\n"
        ".pushsection .text\n"
        ".global user_access_end_put\n"
        "user_access_end_put:\n"
        "ret\n"
        ".popsection\n"
        ".section .fixup,\"ax\"\n"
        "3:\n"
        "mov $-1, %0\n"
        "jmp 2b\n"
        ".previous\n"
        ".section __ex_table,\"a\"\n"
        ".quad 1b,3b\n"
        ".previous"
        : "=r" (success)
        : "r" (udst), "q" (byte)
        : "memory"
    );

    return success == 0;
}


void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	switch (f->R.rax) {
		case SYS_EXIT:
    		exit((int)f->R.rdi);
    		break;

        	case SYS_READ:
            {
                    int fd = (int)f->R.rdi;
                    uint8_t *buffer = (uint8_t *)f->R.rsi;
                    unsigned size = (unsigned)f->R.rdx;
                    if (fd == 0) { // stdin
                        // 유저 주소 유효성 검사 (앞, 뒤 포인터 모두)
                        if (!is_user_vaddr(buffer) || !is_user_vaddr(buffer + size - 1)) {
                            f->R.rax = -1;
                            return;
                        }
                        for (unsigned i = 0; i < size; i++) {
                            char c = input_getc();
                            if (!put_user(buffer + i, c)) {
                                f->R.rax = -1;
                                return;
                            }
                        }
                        f->R.rax = size;
                    }
                break;
            }

			
		case SYS_WRITE:
			{
				int fd = (int)f->R.rdi;
				const void *buffer = (const void *)f->R.rsi;
				unsigned size = (unsigned)f->R.rdx;
				
				if (fd == 1) {  // stdout
					putbuf(buffer, size);
					f->R.rax = size;  // 쓴 바이트 수 반환
				} else {
					f->R.rax = -1;
				}
			}
			break;
        
        case SYS_FORK:
            {
                const char *thread_name = (const char *)f->R.rdi;
                f->R.rax = process_fork(thread_name, f);  // f를 직접 전달
            }
            break;

        case SYS_WAIT:
            {
                tid_t child_pid = f->R.rdi;
                f->R.rax = process_wait(child_pid);
                break;
            }

		
		default:
			printf("Unimplemented system call: %lld\n", f->R.rax);
			thread_exit();
			break;
	}
}

void exit(int status) {
    printf("%s: exit(%d)\n", thread_current()->name, status);
    thread_exit();  // 실제로 스레드 종료
}