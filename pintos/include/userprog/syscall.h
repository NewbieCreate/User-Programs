#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>
#include <stdint.h>

/* 시스템 콜 초기화 */
void syscall_init(void);

/* 사용자 주소에서 바이트를 읽어옵니다.
   실패하면 -1을 반환합니다. */
int64_t get_user(const uint8_t *uaddr);

/* 사용자 주소에 바이트를 씁니다.
   성공 시 true, 실패 시 false 반환 */
bool put_user(uint8_t *udst, uint8_t byte);
void exit(int status);

#endif /* userprog/syscall.h */
