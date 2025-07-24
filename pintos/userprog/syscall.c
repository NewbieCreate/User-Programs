// userprog/syscall.c

#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/palloc.h"  // ADDED: palloc_get_page 사용을 위해

// ADDED: 사용자 메모리 접근에 필요한 헤더 파일들
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "userprog/process.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

// ADDED: 프로세스 강제 종료 헬퍼 함수
static void exit_with_error (void) {
    thread_current()->exit_code = -1;
    thread_exit();
}

// ADDED: 사용자 주소 유효성 검사 함수
static void
validate_user_address (const void *uaddr) {
    struct thread *curr = thread_current();
    if (uaddr == NULL || !is_user_vaddr(uaddr) || pml4_get_page(curr->pml4, uaddr) == NULL) {
        exit_with_error();
    }
}

// ADDED: 사용자 버퍼 유효성 검사 함수
static void
validate_user_buffer (const void *ubuffer, unsigned size) {
    for (unsigned i = 0; i < size; i++) {
        validate_user_address((const char *)ubuffer + i);
    }
}

// ADDED: 사용자 문자열 유효성 검사 함수
static void
validate_user_string (const char *ustr) {
    validate_user_address(ustr);
    while (true) {
        if (*ustr == '\0') {
            break;
        }
        ustr++;
        validate_user_address(ustr);
    }
}


/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081            /* Segment selector msr */
#define MSR_LSTAR 0xc0000082           /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084    /* Mask for the eflags */

void
syscall_init (void) {
    write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
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
syscall_handler (struct intr_frame *f) { // MODIFIED: UNUSED 제거
    // 시스템 콜 넘버는 %rax 레지스터에 저장되어 전달됩니다.
    switch (f->R.rax) {
        case SYS_EXIT:
            {
                // MODIFIED: exit 시스템 콜의 인자는 status 하나입니다.
                int status = (int)f->R.rdi;
                thread_current()->exit_code = status; // 스레드 종료 코드 저장
                printf("%s: exit(%d)\n", thread_current()->name, status);
                thread_exit();
            }
            break;

        case SYS_WRITE:
            {
                int fd = (int)f->R.rdi;
                const void *buffer = (const void *)f->R.rsi;
                unsigned size = (unsigned)f->R.rdx;

                // MODIFIED: buffer 포인터와 메모리 영역 검증
                validate_user_buffer(buffer, size);

                if (fd == 1) {  // stdout
                    putbuf(buffer, size);
                    f->R.rax = size;  // 쓴 바이트 수 반환
                } else {
                    // TODO: 파일 디스크립터가 0 (stdin) 이거나 다른 파일일 경우 처리
                    f->R.rax = -1; // 현재는 stdout 외에는 실패 처리
                }
            }
            break;

        /* 여기에 다른 시스템 콜 케이스들을 추가하게 됩니다. (e.g., SYS_OPEN, SYS_READ...) */
        // 예시: SYS_OPEN
        // case SYS_OPEN:
        // {
        //     const char *file_name = (const char *)f->R.rdi;
        //     validate_user_string(file_name);
        //     // ... 파일 여는 로직 ...
        // }
        // break;

        default:
            printf("Unimplemented system call: %lld\n", f->R.rax);
            exit_with_error(); // MODIFIED: 정의되지 않은 시스템콜은 에러 처리로 종료
            break;
    }
}