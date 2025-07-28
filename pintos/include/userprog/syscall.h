#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h> 
#include <stdint.h>
#include "threads/synch.h"

int64_t get_user (const uint8_t *uaddr);
void syscall_init (void);
bool put_user (uint8_t *udst, uint8_t byte);

#endif /* userprog/syscall.h */
