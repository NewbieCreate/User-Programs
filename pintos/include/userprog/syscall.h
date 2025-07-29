#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h> 
#include <stdint.h>
#include "threads/synch.h"

int64_t get_user (const uint8_t *uaddr);
void syscall_init (void);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned length);
int write(int fd, const void *buffer, unsigned length);
void seek(int fd, unsigned position);
int tell(int fd);
bool put_user (uint8_t *udst, uint8_t byte);
int add_file_to_fdt(struct file *file);
void remove_file_from_fdt(int fd);
void remove_all_fd(struct thread *t);
#endif /* userprog/syscall.h */
