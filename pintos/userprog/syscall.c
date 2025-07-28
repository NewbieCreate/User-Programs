#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/flags.h"
#include "userprog/gdt.h"
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/palloc.h"
#include "userprog/process.h"
#include "vm/vm.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void check_address(const void *addr);
struct lock filesys_lock;
static struct file *find_file_by_fd(int fd);
int add_file_to_fdt(struct file *file);
void remove_file_from_fdt(int fd);

int exec(char *file_name);
tid_t fork(const char *thread_name, struct intr_frame *f);
void halt(void);
void exit(int status);
int wait(tid_t tid);
bool create(const char *file, unsigned intial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
void close(int fd);


int64_t
get_user (const uint8_t *uaddr) {
    int64_t result;
    __asm __volatile (
    "movabsq $done_get, %0\n"
    "movzbq %1, %0\n"
    "done_get:\n"
    : "=&a" (result) : "m" (*uaddr));
    return result;
}

bool
put_user (uint8_t *udst, uint8_t byte) {
    int64_t error_code;
    __asm __volatile (
    "movabsq $done_put, %0\n"
    "movb %b2, %1\n"
    "done_put:\n"
    : "=&a" (error_code), "=m" (*udst) : "q" (byte));
    return error_code != -1;
}

int add_file_to_fdt(struct file *file)
{
	struct thread *cur = thread_current();
	struct file **fdt = cur->fd_table;

	while(cur->fd_idx < FDCOUNT_LIMIT && fdt[cur->fd_idx])
	{
		cur->fd_idx++;
	}

	if(cur->fd_idx >= FDCOUNT_LIMIT)
	{
		return -1;
	}

	fdt[cur->fd_idx = FDCOUNT_LIMIT] = file;
	return cur->fd_idx;
}

static struct file *find_file_by_fd(int fd)
{
	struct thread *cur = thread_current();
	if (fd<0 || fd >= FDCOUNT_LIMIT)
	{
		return NULL;
	}
	return cur->fd_table[fd];
}

void remove_file_from_fdt(int fd)
{
	struct thread *cur = thread_current();

	if(fd < 0 || fd >= FDCOUNT_LIMIT)
	{
		return;
	}
	cur->fd_table[fd] = NULL;
}

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
	
	lock_init(&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	int sys_number = f->R.rax;
	switch(sys_number)
	{
		case SYS_HALT :
		{
			halt();
			break;
		}
		case SYS_EXIT:
		{
			exit(f->R.rdi);
			break;
		}
		case SYS_FORK :
		{
			f->R.rax = fork(f->R.rdi, f);
			break;
		}
		case SYS_EXEC :
		{
			if(exec(f->R.rdi) == -1)
			{
				exit(-1);
			}
			break;
		}
		case SYS_WAIT :
		{
			f->R.rax = process_wait(f->R.rdi);
			break;
		}
		case SYS_CREATE :
		{
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		}
		case SYS_REMOVE :
		{
			f->R.rax = remove(f->R.rdi);
			break;
		}
		case SYS_OPEN :
		{
			f->R.rax = open(f->R.rdi);
			break;
		}
		case SYS_FILESIZE :
		{
			f->R.rax =filesize(f->R.rdi);
			break;
		}
		case SYS_READ :
		{
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		}
		case SYS_WRITE :
		{
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		}
		// case SYS_SEEK :
		// {
		// 	seek(f->R.rdi, f->R.rsi);
		// 	break;
		// }
		// case SYS_TELL :
		// {
		// 	f->R.rax = tell(f->R.rdi);
		// 	break;
		// }
		case SYS_CLOSE :
		{
			close(f->R.rdi);
			break;
		}
		default :
		{
			exit(-1);
			break;
		}
	}
}

void check_address(const void *addr)
{
	struct thread *t = thread_current();

	/*포인터가 가리키는 주소가 유저영역의 주소인지 확인 
	|| 포인터가 가리키는 주소가 유저 영역 내에 있지만 
	페이지로 할당하지 않은 영역일수도 잇으니 체크*/

	if(!is_user_vaddr(addr) || addr == NULL || pml4_get_page(t->pml4, addr) == NULL)
	{
		exit(-1); /* 잘못 접근 했을 경우 프로세스 종료 */
	}
}

void halt(void){
	power_off();
}

/* 현재 프로세스를 종료시키는 시스템 콜 */
void exit(int status){
	struct thread *cur = thread_current();
    cur->exit_status = status;		// 프로그램이 정상적으로 종료되었는지 확인(정상적 종료 시 0)
	printf("%s: exit(%d)\n", thread_name(), status);
 	thread_exit();		// 스레드 종료
}

int exec(char *file_name)
{
	check_address(file_name);

	int file_size = strlen(file_name)+1;
	char *fn_copy = palloc_get_page(PAL_ZERO);
	if(fn_copy == NULL)
	{
		exit(-1);
	}
	strlcpy(fn_copy, file_name, file_size);

	if(process_exec(fn_copy) -1)
	{
		return -1;
	}

	NOT_REACHED();
	return 0;
}

bool create(const char *file, unsigned initial_size)
{
	check_address(file);
	return filesys_create(file, initial_size);
}

bool remove(const char *file)
{
	check_address(file);
	return filesys_remove(file);
}

int open(const char *file)
{
	check_address(file);
	struct file *open_file = filesys_open(file);

	if(open_file == NULL)
	{
		return -1;
	}

	int fd = add_file_to_fdt(open_file);

	if(fd == -1)
	{
		file_close(open_file);
	}
	return fd;
}

int filesize(int fd)
{
	struct file *open_file = find_file_by_fd(fd);
	if(open_file == NULL)
	{
		return -1;
	}
	return file_length(open_file);
}

tid_t fork(const char *thread_name, struct intr_frame *f)
{
	return process_fork(thread_name, f);
}

int read(int fd, void *buffer, unsigned size)
{
	int read_result;
	struct thread *cur = thread_current();
	struct file *file_fd = find_file_by_fd(fd);

	if(fd == 0)
	{
		*(char *)buffer = input_getc();
		read_result = size;
	}else
	{
		if(find_file_by_fd(fd) == NULL)
		{
			return -1;
		}else
		{
			lock_acquire(&filesys_lock);
			read_result = file_read(find_file_by_fd(fd), buffer, size);
			lock_release(&filesys_lock);
		}
	}
	return read_result;

}
/*  */
int write(int fd, const void *buffer, unsigned size)
{
	check_address(buffer);
	lock_acquire(&filesys_lock);
	int write_result;

	if(fd == 1)
	{
		putbuf(buffer, size);
		write_result = size;
	}else{
		if (find_file_by_fd(fd) != NULL)
		{
			write_result = file_write(find_file_by_fd(fd), buffer, size);
		}else{
			write_result = -1;
		}
	}
	lock_release(&filesys_lock);
	return write_result;
}

// /* 파일 위치로 이동하는 함수 */
// void seek(int fd, unsigned poition)
// {
// 	struct file *fileobj = find_file_by_fd(fd);
// 	if(fileobj <= 2)
// 	{
// 		return;
// 	}
// 	fileobj->pos = poition;
// }

// /* 파일 위치을 알려주는 함수 */
// unsigned tell(int fd)
// {
// 	struct file *fileobj = find_file_by_fd(fd);
// 	if(fileobj <= 2) return;
// 	return file_tell(fileobj);
// }

void close(int fd)
{
	struct file *fileobj = find_file_by_fd(fd);
	if(fileobj == NULL)
	{
		return;
	}
	remove_file_from_fdt(fd);
}


