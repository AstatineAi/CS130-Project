#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "devices/shutdown.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  int syscall_id = *(int *)f->esp;
  switch (syscall_id)
    {
      case SYS_HALT: syscall_halt(); break;
      case SYS_EXIT: syscall_exit(f->esp); break;
      case SYS_WRITE: syscall_write(f->esp); break;
      default : PANIC("Unknown system call id!"); break;
    }
  /* Restore stack pointer. */
}

void
syscall_halt (void) {
  shutdown_power_off ();
}

void
syscall_exit (const void *esp)
{
  struct thread *cur = thread_current ();
  int return_status = *(int *)(esp + sizeof(int *));
  printf ("%s: exit(%d)\n", cur->name, return_status);
  cur->return_status = return_status;
  thread_exit ();
}

void
syscall_write (const void *esp)
{
  int fd = *(int *)(esp + sizeof(int *));
  const void *buffer = *(const void **)(esp + 2 * sizeof(int *));
  unsigned size = *(unsigned *)(esp + 3 * sizeof(int *));
  if (fd == 1)
    {
      putbuf (buffer, size);
    }
  else
    {
      PANIC("NOT IMPLEMENTED.");
    }
}
