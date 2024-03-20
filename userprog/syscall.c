#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "../include/threads/init.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/flags.h"
#include "threads/synch.h"
#include "userprog/gdt.h"
#include "userprog/process.h"

#include "filesys/filesys.h"
#include "filesys/file.h"
#include "kernel/stdio.h"
#include "threads/palloc.h"
#include "intrinsic.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
void check_address(void *addr);
void halt(void);
void exit(int status);

int open(const char *file);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081			/* Segment selector msr */
#define MSR_LSTAR 0xc0000082		/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void)
{
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
							((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			  FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	lock_init(&filesys_lock);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f)
{
	int syscall_num = f->R.rax;

	switch (syscall_num)
	{
	case SYS_HALT:
		halt();
		break;

	case SYS_EXIT:
		exit(f->R.rdi);
		break;

	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		break;

	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;

	default:
		break;
	}
}

void check_address(void *addr)
{
	struct thread *curr = thread_current();

	if (pml4_get_page(curr->pml4, addr) == NULL)
	{
		exit(-1);
	}
	if (!is_user_vaddr(addr) || addr == NULL)
	{
		exit(-1);
	}
}

void halt(void)
{
	power_off();
}

void exit(int status)
{
	struct thread *curr = thread_current();
	printf("%s: exit(%d)\n", curr->name, status);
	thread_exit();
}

int open(const char *file_name)
{
	check_address(file_name);

	struct file *file = filesys_open(file_name);

	if (file == NULL)
	{
		return -1;
	}

	int fd = process_add_file(file);

	if (fd == -1)
	{
		file_close(file);
		return -1;
	}

	return fd;
}

int write(int fd, const void *buffer, unsigned size)
{
	check_address(buffer);
	struct file *file = process_get_file(fd);

	lock_acquire(&filesys_lock);

	int bytes_written = 0;

	if (fd == 0)
	{
		lock_release(&filesys_lock);
		return -1;
	}
	else if (fd == 1)
	{
		putbuf(buffer, size);
		bytes_written = size;
	}
	else if (fd >= 2)
	{
		if (file == NULL)
		{
			lock_release(&filesys_lock);
			return -1;
		}
		bytes_written = file_write(file, buffer, size);
	}

	lock_release(&filesys_lock);

	return bytes_written;
}