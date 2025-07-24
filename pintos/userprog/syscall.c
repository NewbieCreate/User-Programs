#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/init.h"      // ADDED: power_off()
#include "threads/synch.h"     // ADDED: lock
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "userprog/process.h"
#include "filesys/filesys.h"   // ADDED: filesys_create, filesys_remove
#include "filesys/file.h"      // ADDED: file operations

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

// ADDED: 파일 시스템 동기화를 위한 전역 lock
static struct lock filesys_lock;

// 기존 헬퍼 함수들...
static void exit_with_error (void) {
    thread_current()->exit_code = -1;
    thread_exit();
}

static void
validate_user_address (const void *uaddr) {
    struct thread *curr = thread_current();
    if (uaddr == NULL || !is_user_vaddr(uaddr) || pml4_get_page(curr->pml4, uaddr) == NULL) {
        exit_with_error();
    }
}

static void
validate_user_buffer (const void *ubuffer, unsigned size) {
    for (unsigned i = 0; i < size; i++) {
        validate_user_address((const char *)ubuffer + i);
    }
}

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

/* MSR 정의들... */
#define MSR_STAR 0xc0000081
#define MSR_LSTAR 0xc0000082
#define MSR_SYSCALL_MASK 0xc0000084

void
syscall_init (void) {
    write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
        ((uint64_t)SEL_KCSEG) << 32);
    write_msr(MSR_LSTAR, (uint64_t) syscall_entry);
    write_msr(MSR_SYSCALL_MASK,
            FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
    
    // ADDED: 파일 시스템 lock 초기화
    lock_init(&filesys_lock);
}

void
syscall_handler (struct intr_frame *f) {
    switch (f->R.rax) {
        case SYS_HALT:
            power_off();
            break;
            
        case SYS_EXIT:
            {
                int status = (int)f->R.rdi;
                thread_current()->exit_code = status;
                printf("%s: exit(%d)\n", thread_current()->name, status);
                thread_exit();
            }
            break;

        case SYS_CREATE:
            {
                const char *file = (const char *)f->R.rdi;
                unsigned initial_size = (unsigned)f->R.rsi;
                
                validate_user_string(file);
                
                lock_acquire(&filesys_lock);
                bool success = filesys_create(file, initial_size);
                lock_release(&filesys_lock);
                
                f->R.rax = success;
            }
            break;

        case SYS_REMOVE:
            {
                const char *file = (const char *)f->R.rdi;
                
                validate_user_string(file);
                
                lock_acquire(&filesys_lock);
                bool success = filesys_remove(file);
                lock_release(&filesys_lock);
                
                f->R.rax = success;
            }
            break;

        case SYS_WRITE:
            {
                int fd = (int)f->R.rdi;
                const void *buffer = (const void *)f->R.rsi;
                unsigned size = (unsigned)f->R.rdx;

                validate_user_buffer(buffer, size);

                if (fd == 1) {  // stdout
                    putbuf(buffer, size);
                    f->R.rax = size;
                } else {
                    // TODO: 파일 디스크립터 처리
                    f->R.rax = -1;
                }
            }
            break;

        default:
            printf("Unimplemented system call: %lld\n", f->R.rax);
            exit_with_error();
            break;
    }
}