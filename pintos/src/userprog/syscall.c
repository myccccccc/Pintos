#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include <stdio.h>
#include <stdbool.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "devices/shutdown.h"
#include "filesys/inode.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/directory.h"
#include "filesys/free-map.h"
#include <list.h>
#include "devices/input.h"

static void syscall_handler (struct intr_frame *);
static void access_user_memory(uint32_t* vaddr, struct intr_frame *f);
static bool proc_create(const char* file, unsigned initial_size);
static bool proc_remove(const char* file);
static int proc_open(const char* file);
static void proc_close(int fd);
static int proc_read(int fd, void* buffer, unsigned size);
static int proc_write(int fd, const void *buffer, unsigned size);
static int proc_filesize (int fd);
static void proc_seek (int fd, unsigned position);
static unsigned proc_tell (int fd);
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
  else if (args[0] == SYS_HALT)
  {
    proc_halt();
  }
  else if (args[0] == SYS_EXEC)
  {
    access_user_memory(args+1, f);
    access_user_memory((uint32_t*) *(args+1), f);
    f->eax = proc_exec((const char *) args[1]);
  }
  else if (args[0] ==SYS_WAIT)
  {
    access_user_memory(args+1, f);
    f->eax = proc_wait(args[1]);
  }
  else if (args[0] == SYS_EXIT)
  {
    access_user_memory(args+1, f);
    proc_exit(args[1], f);
  }
  else if (args[0] == SYS_CREATE) {
    access_user_memory(args+1, f);
    access_user_memory((uint32_t*) *(args + 1), f);
    access_user_memory(args+2, f);
    filesys_lock_acuqire();
    f->eax = (int) proc_create((const char*) args[1], args[2]);
    filesys_lock_release();
  }
  else if (args[0] == SYS_REMOVE) {
    access_user_memory(args + 1, f);
    access_user_memory((uint32_t*) *(args+1), f);
    filesys_lock_acuqire();
    f->eax = (int) proc_remove((const char*) args[1]);
    filesys_lock_release();
  }
  else if (args[0] == SYS_OPEN) {
    access_user_memory(args+1, f);
    access_user_memory((uint32_t*) *(args+1), f);
    filesys_lock_acuqire();
    f->eax = proc_open((const char*) args[1]);
    filesys_lock_release();
  }
  else if (args[0] == SYS_CLOSE) {
    access_user_memory(args+1, f);
    filesys_lock_acuqire();
    proc_close(args[1]);
    filesys_lock_release();
  }
  else if (args[0] == SYS_READ) {
    access_user_memory(args+1, f);
    access_user_memory(args+2, f);
    access_user_memory(args+3, f);
    access_user_memory((uint32_t*) *(args+2), f);
    filesys_lock_acuqire();
    f->eax = proc_read(args[1], (void*) args[2], args[3]);
    filesys_lock_release();
  }
  else if (args[0] == SYS_WRITE)
  {
    access_user_memory(args+1, f);
    access_user_memory(args+2, f);
    access_user_memory(args+3, f);
    access_user_memory((uint32_t*)*(args+2), f);
    filesys_lock_acuqire();
    f->eax = proc_write(args[1], (const void *) args[2], args[3]);
    filesys_lock_release();
  }
  else if (args[0] == SYS_FILESIZE) {
    access_user_memory(args+1, f);
    filesys_lock_acuqire();
    f->eax = proc_filesize(args[1]);
    filesys_lock_release();
  }
  else if (args[0] == SYS_SEEK) {
    access_user_memory(args+1, f);
    access_user_memory(args+2, f);
    filesys_lock_acuqire();
    proc_seek(args[1], args[2]);
    filesys_lock_release();
  }
  else if (args[0] == SYS_TELL) {
    access_user_memory(args+1, f);
    filesys_lock_acuqire();
    f->eax = (int) proc_tell(args[1]);
    filesys_lock_release();
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
  return process_wait(pid);
}

static bool proc_create(const char* f, unsigned initial_size) {
    bool created = filesys_create(f, (off_t)  initial_size);
    return created;
}

static bool proc_remove(const char* f) {
    bool removed = filesys_remove(f);
    return removed;
}

static int proc_open(const char* f) {
    struct file* opened = filesys_open(f);
    if (opened == NULL) {
        return -1;
    }
    else {
        if (list_size(&thread_current()->process_file_map) >= MAX_FD_NUM)
        {
          file_close(opened);
          return -1;
        }
        create_and_push_back_pfme(opened);
        struct list_elem* last = list_back(&thread_current()->process_file_map);
        struct process_file_map_elem* pfme_last = list_entry(last, struct process_file_map_elem, elem);
        //printf("NEW FILE DESCRIPTOR IS %d\n", pfme_last->fd);
        return pfme_last->fd;
    }
}

static void proc_close(int fd) {
    remove_pfme_by_fd(fd);
}

static int proc_read(int fd, void* buffer, unsigned size) {
  if (fd == 0) {
    unsigned int numCharsRead;
    for (numCharsRead = 0; numCharsRead < size; numCharsRead ++) {
      uint8_t* cast = (uint8_t*) buffer;
      cast[numCharsRead] = input_getc();
    }
    return (int) size;
  }
  else if (fd != 1) {
    struct list_elem* index;
    for (index = list_begin(&thread_current()->process_file_map); index != list_end(&thread_current()->process_file_map); index = list_next(index)) {
      struct process_file_map_elem* pfme = list_entry(index, struct process_file_map_elem, elem);
      if (pfme->fd == fd) {
        int num_read = (int) file_read(pfme->file, buffer, (off_t) size);
        return num_read;
      }
    }
    return -1;
  }
  return -1;
}

static int proc_write(int fd, const void *buffer, unsigned size)
{
  if (fd == 1)
  {
    putbuf((char *) buffer, size);
    return (int) size;
  }
  else if (fd != 0) {
    struct list_elem* index;
    for (index = list_begin(&thread_current()->process_file_map); index != list_end(&thread_current()->process_file_map); index = list_next(index)) {
      struct process_file_map_elem* pfme = list_entry(index, struct process_file_map_elem, elem);
      if (pfme->fd == fd) {
        int num_written = (int) file_write(pfme->file, buffer, (off_t) size);
        return num_written;
      }
    }
    return 0;
  }
  return 0;
}

static int proc_filesize(int fd) {
  struct list_elem* index;
  for (index = list_begin(&thread_current()->process_file_map); index != list_end(&thread_current()->process_file_map); index = list_next(index)) {
    struct process_file_map_elem* pfme = list_entry(index, struct process_file_map_elem, elem);
    if (pfme->fd == fd) {
      return file_length(pfme->file);
    }
  }
  return -1;
}

static void proc_seek(int fd, unsigned position) {
  struct list_elem* index;
  for (index = list_begin(&thread_current()->process_file_map); index != list_end(&thread_current()->process_file_map); index = list_next(index)) {
    struct process_file_map_elem* pfme = list_entry(index, struct process_file_map_elem, elem);
    if (pfme->fd == fd) {
      file_seek(pfme->file, (off_t) position);
      break;
    }
  }
}

static unsigned proc_tell(int fd) {
  struct list_elem* index;
  for (index = list_begin(&thread_current()->process_file_map); index != list_end(&thread_current()->process_file_map); index = list_next(index)) {
    struct process_file_map_elem* pfme = list_entry(index, struct process_file_map_elem, elem);
    if (pfme->fd == fd) {
      return (unsigned) file_tell(pfme->file);
    }
  }
  return 0;
}
