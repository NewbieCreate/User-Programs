#ifndef __LIB_KERNEL_CONSOLE_H
#define __LIB_KERNEL_CONSOLE_H

#include <stddef.h>  // ← 이 줄 추가

void console_init (void);
void console_panic (void);
void console_print_stats (void);
void putbuf (const char *buffer, size_t n);

#endif /* lib/kernel/console.h */
