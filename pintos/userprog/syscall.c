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
	struct thread *curr = thread_current();
	/* 1. 리스트 크기 제한검사 */
	if(list_size(&curr->fd_list) >= FDCOUNT_LIMIT)
	{
		return -1;
	}

	/* 2. FD 번호 제한검사 */
	if(curr->last_created_fd >= FDCOUNT_LIMIT)
	{
		return -1;
	}

	/* 파일 디스크립터 구조체 동적할당 */
	struct file_descriptor *fd_entry = malloc(sizeof(struct file_descriptor));
	if (fd_entry == NULL)
	{
		return -1;
	}

	fd_entry->fd = curr->last_created_fd++;
	fd_entry->file_p = file;
	list_push_back(&curr->fd_list, &fd_entry->fd_elem);

	return fd_entry->fd;
}

static struct file *find_file_by_fd(int fd)
{

   struct thread *curr = thread_current();
 
    if (fd < 0 || fd >= FDCOUNT_LIMIT)
        return NULL;
 
    struct list_elem *e;
    for (e = list_begin(&curr->fd_list); e != list_end(&curr->fd_list); e = list_next(e)) {
        struct file_descriptor *fd_entry = list_entry(e, struct file_descriptor, fd_elem);
        if (fd_entry->fd == fd)
            return fd_entry->file_p;
    }
 
    return NULL;  // 해당 FD를 찾지 못한 경우
}

void remove_file_from_fdt(int fd)
{
	struct thread *cur = thread_current();
	if(fd < 0 || fd >= FDCOUNT_LIMIT)
	{
		return -1;
	}

	struct list_elem *e;
	for(e = list_begin(&cur->fd_list); e != list_end(&cur->fd_list); e = list_next(e))
	{
		struct file_descriptor *fd_entry = list_entry(e, struct file_descriptor, fd_elem);
		if(fd_entry->fd == fd)
		{
			list_remove(&fd_entry->fd_elem);
			if(fd_entry->file_p != NULL)
			{
			file_close(fd_entry->file_p);
		    }
			free(fd_entry);
		    return 0;
		}
	}

	return -1;
}

void remove_all_fd(struct thread *t)
{
	if(t == NULL)
	{
		return;
	}

	while(!list_empty(&t->fd_list))
	{
		struct list_elem *e = list_pop_front(&t->fd_list);
		struct file_descriptor *fd_entry = list_entry(e, struct file_descriptor, fd_elem);

		if(fd_entry != NULL)
		{
			if(fd_entry->file_p != NULL)
			{
				file_close(fd_entry->file_p);
				free(fd_entry);
			}
		}
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
		case SYS_SEEK :
		{
			seek(f->R.rdi, f->R.rsi);
			break;
		}
		case SYS_TELL :
		{
			f->R.rax = tell(f->R.rdi);
			break;
		}
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
	if(is_kernel_vaddr(addr) || addr == NULL || pml4_get_page(thread_current()->pml4, addr) == NULL)
	{
		exit(-1);
	}
}

void halt(void){
	power_off();
}

/* 현재 프로세스를 종료시키는 시스템 콜 */
void exit(int status){
	struct thread *cur = thread_current();
    cur->exit_status = status;		// 프로그램이 정상적으로 종료되었는지 확인(정상적 종료 시 0)
	printf("%s: exit(%d)\n", cur->name, cur->exit_status);
 	thread_exit();		// 스레드 종료
}

int exec(char *file_name)
{
	check_address(file_name);
	
	off_t size = strlen(file_name)+1;
	char *cmd_copy = palloc_get_page(PAL_ZERO);

	if(cmd_copy == NULL)
	{
		return -1;
	}

	memcpy(cmd_copy, file_name, size);

	if(process_exec(cmd_copy) == -1)
	{
		return -1;
	}

	return 0;
}

bool 
create(const char *file, unsigned initial_size)
{
	check_address(file);
	return filesys_create(file, initial_size);
}

bool 
remove(const char *file)
{
	check_address(file);
	return filesys_remove(file);
}

int open(const char *file)
{
	check_address(file);

	lock_acquire(&filesys_lock);
	struct file *newfile = filesys_open(file);
	lock_release(&filesys_lock);

	if(newfile == NULL)
	{
		return -1;
	}

	int fd = add_file_to_fdt(newfile);

	if(fd == -1)
	{
		file_close(newfile);
		return -1;
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
	check_address(thread_name);
	return process_fork(thread_name, f);
}

int read(int fd, void *buffer, unsigned size)
{
	check_address(buffer);

	if(fd == 0) //0(stdin) -> 키보드로 직접입력
	{
		int i = 0; //쓰레기 값 리턴방지
		char c;
		unsigned char *buf = buffer;

		for(; i<size; i++)
		{
			c = input_getc();
			*buf++ = c;
			if(c == '\0')
			{
				break;
			}
		}

		return i;
	}
	if(fd < 3) //std, stdrr를 읽으려 할 경우 & fd가 음스일때
	{
		return -1;
	}
	struct file *file = find_file_by_fd(fd);
	off_t byte = -1;

	if(file == NULL)
	{
		return -1;
	}
	lock_acquire(&filesys_lock);
	byte = file_read(file,buffer,size);
	lock_release(&filesys_lock);
	
	return byte;
}
/*  */
int write(int fd, const void *buffer, unsigned size)
{
	check_address(buffer);

	off_t bytes = -1;

	if(fd <= 0) //쓰려고 하거나 fd가 음수일경우
	{
		return -1;
	}

	if(fd<3) //1(stdout) 2(stderr) -> 콘솔로 출력
	{
		putbuf(buffer, size);
		return size;
	}

	struct file *file = find_file_by_fd(fd);

	if(file == NULL)
	{
		return -1;
	}

	lock_acquire(&filesys_lock);
	bytes = file_write(file, buffer, size);
	lock_release(&filesys_lock);

	return bytes;
}

// /* 파일 위치로 이동하는 함수 */
void seek(int fd, unsigned position) 
{
    struct file *file = find_file_by_fd(fd);
 
    if (fd < 3 || file == NULL)
        return;
 
    file_seek(file, position);
}

int tell(int fd)
{
	struct file *file = find_file_by_fd(fd);

	if(fd < 3 || file == NULL)
	{
		return -1;
	}

	return file_tell(file);
}

void close(int fd)
{
	struct file *file = find_file_by_fd(fd);

	if(fd<3||file==NULL)
	{
		return;
	}

	remove_file_from_fdt(fd);
}


