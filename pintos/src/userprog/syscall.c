#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);
static void access_user_memory(uint32_t* vaddr, struct intr_frame *f);
static int proc_write(int fd, const void *buffer, unsigned size);
static int proc_practice (int i);
static void proc_halt (void);
static int proc_exec (const char *file);
static void proc_exit(int status, struct intr_frame *f);
static int proc_wait (int pid);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  uint32_t* args = ((uint32_t*) f->esp);
  access_user_memory(args, f);
  //printf("System call number: %d\n", args[0]);

  if (args[0] == SYS_PRACTICE)
  {
    access_user_memory(args+1, f);
    f->eax = proc_practice(args[1]);
  }
  if (args[0] == SYS_HALT)
  {
    proc_halt();
  }
  if (args[0] == SYS_EXEC)
  {
    access_user_memory(args+1, f);
    access_user_memory((uint32_t*) *(args+1), f);
    f->eax = proc_exec((const char *) args[1]);
  }
  if (args[0] ==SYS_WAIT)
  {
    access_user_memory(args+1, f);
    f->eax = proc_wait(args[1]);
  }
  if (args[0] == SYS_EXIT)
  {
    access_user_memory(args+1, f);
    proc_exit(args[1], f);
  }
  if (args[0] == SYS_WRITE)
  {
    access_user_memory(args+1, f);
    access_user_memory(args+2, f);
    access_user_memory(args+3, f);
    access_user_memory((uint32_t*)*(args+2), f);
    f->eax = proc_write(args[1], (void *) args[2], args[3]);
  } 
}

static void access_user_memory(uint32_t* vaddr, struct intr_frame *f)
{
	if (!is_user_vaddr(vaddr))
	{
		proc_exit(-1, f);
	}
	if (pagedir_get_page(thread_current()->pagedir, vaddr) == NULL)
	{
		proc_exit(-1, f);
	}
}

static int proc_practice (int i)
{
  return i + 1;
}

static void proc_halt(void)
{
  shutdown_power_off();
}

static void proc_exit(int status, struct intr_frame *f)
{
  f->eax = status;
  thread_current()->wait_status->exit_code = status;
  printf("%s: exit(%d)\n", (char *) &thread_current ()->name, status);
  thread_exit();
}

static int proc_exec (const char *file)
{
  int pid = process_execute(file);
  if (get_tid_wait_status(pid)->load_status == -1)
  {
    return -1;
  }
  return pid;
}

static int proc_wait (int pid)
{
  struct wait_status *tid_wait_status;
  tid_wait_status = get_tid_wait_status(pid);
  int tid_exit_code;
  if (tid_wait_status == NULL)
  {
    return -1;
  }
  else
  {
    sema_down(&tid_wait_status->dead);
    tid_exit_code = tid_wait_status->exit_code;
    list_remove(&tid_wait_status->elem);
    free(tid_wait_status);
    return tid_exit_code;
  }
  
}

static int proc_write(int fd, const void *buffer, unsigned size)
{
  if (fd == 1)
  {
    putbuf((char *) buffer, size);
    return (int) size;
  }
  return 0;
}
