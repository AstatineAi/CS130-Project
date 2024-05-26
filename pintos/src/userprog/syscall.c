#include "userprog/syscall.h"
#include <stdint.h>
#include <stdio.h>
#include <syscall-nr.h>
#include <stddef.h>

#include "devices/shutdown.h"
#include "devices/input.h"
#include "list.h"
#include "stdbool.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "vm/frame.h"

static void syscall_handler (struct intr_frame *);

static int get_user (const uint8_t *);
static bool put_user (uint8_t *, uint8_t);
static void *validate_user_read_ptr (const void *, size_t);
static void *validate_user_write_ptr (void *, size_t);
static bool validate_user_string (const char *, size_t);

static struct file_descriptor *get_file_by_fd (int);

static void syscall_halt (void);
static tid_t syscall_exec (const char *);
static int syscall_wait (tid_t);
static bool syscall_create (const char *, unsigned);
static bool syscall_remove (const char *);
static int syscall_open (const char *);
static int syscall_filesize (int);
static int syscall_read (int, void *, unsigned);
static int syscall_write (int, const void *, unsigned);
static void syscall_seek (int, unsigned);
static unsigned syscall_tell (int);
static void syscall_close (int);
static mapid_t syscall_mmap (int, void *);

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

static void *
validate_user_read_ptr (const void *vaddr, size_t size)
{
  if (!is_user_vaddr (vaddr))
      syscall_exit (-1);
  for (size_t i = 0; i < size; i++)
    {
      if (get_user (vaddr + i) == -1)
        syscall_exit (-1);
    }
  return (void *) vaddr;
}

static void *
validate_user_write_ptr (void *vaddr, size_t size)
{
  if (!is_user_vaddr (vaddr))
      syscall_exit (-1);
  for (size_t i = 0; i < size; i++)
    {
      if (!put_user (vaddr + i, 0))
        syscall_exit (-1);
    }
  return (void *) vaddr;
}

static bool
validate_user_string (const char *str, size_t constraint)
{
  const uint8_t *ptr = validate_user_read_ptr (str, sizeof (char));
  size_t i = 0;
  while (i < constraint)
    {
      int ch = get_user (ptr + i);
      if (ch == -1)
        syscall_exit (-1);
      if (ch == '\0')
        break;
      i++;
    }
  if (i == constraint)
    return false;
  return true;
}

/* Get struct file_descriptor by fd id, return 
   NULL if not found. */
static struct file_descriptor *
get_file_by_fd (int fd)
{
  struct thread *cur = thread_current ();
  for (struct list_elem *e = list_begin (&cur->fd_list);
       e != list_end (&cur->fd_list); e = list_next (e))
    {
      struct file_descriptor *fd_elem = list_entry (e, struct file_descriptor, elem);
      if (fd_elem->fd == fd)
        return fd_elem;
    }
  return NULL;
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int syscall_id = *(int *) validate_user_read_ptr (f->esp, sizeof (int));
#ifdef VM
  thread_current ()->save_esp = f->esp;
#endif
  switch (syscall_id)
    {
      case SYS_HALT:
        syscall_halt ();
        break;
      case SYS_EXIT:
        {
          int status = *(int *) validate_user_read_ptr (f->esp + sizeof (int *),
                                                        sizeof (int));
          syscall_exit (status);
          NOT_REACHED ();
        }
      case SYS_EXEC:
        {
          const char *cmd_line = *(const char **)validate_user_read_ptr (f->esp + sizeof (int *),
                                                                         sizeof (char *));
          f->eax = (uint32_t) syscall_exec (cmd_line);
          break;
        }
      case SYS_WAIT:
        {
          tid_t tid = *(tid_t *) validate_user_read_ptr (f->esp + sizeof (int *),
                                                         sizeof (tid_t));
          f->eax = (uint32_t) syscall_wait (tid);
          break;
        }
      case SYS_CREATE:
        {
          const char *name = *(const char **) validate_user_read_ptr (f->esp + sizeof (int *),
                                                                      sizeof (char *));
          unsigned initial_size = *(unsigned *) validate_user_read_ptr (f->esp + 2 * sizeof (int *),
                                                                        sizeof (unsigned));
          f->eax = (uint32_t) syscall_create (name, initial_size);
          break;
        }
      case SYS_REMOVE:
        {
          const char *name = *(const char **) validate_user_read_ptr (f->esp + sizeof (int *),
                                                                      sizeof (char *));
          f->eax = (uint32_t) syscall_remove (name);
          break;
        }
      case SYS_OPEN:
        {
          const char *name = *(const char **) validate_user_read_ptr (f->esp + sizeof (int *),
                                                                      sizeof (char *));
          f->eax = (uint32_t) syscall_open (name);
          break;
        }
      case SYS_FILESIZE:
        {
          int fd = *(int *) validate_user_read_ptr (f->esp + sizeof (int *),
                                                    sizeof (int));
          f->eax = (uint32_t) syscall_filesize (fd);
          break;
        }
      case SYS_READ:
        {
          int fd = *(int *) validate_user_read_ptr (f->esp + sizeof (int *),
                                                    sizeof (int));
          void *buffer = *(void **) validate_user_read_ptr (f->esp + 2 * sizeof (int *),
                                                            sizeof (void *));
          unsigned size = *(unsigned *) validate_user_read_ptr (f->esp + 3 * sizeof (int *),
                                                                sizeof (unsigned));
          f->eax = (uint32_t) syscall_read (fd, buffer, size);
          break;
        }
      case SYS_WRITE:
        {
          int fd = *(int *) validate_user_read_ptr (f->esp + sizeof (int *),
                                                    sizeof (int));
          const void *buffer = *(const void **) validate_user_read_ptr (f->esp + 2 * sizeof (int *),
                                                                       sizeof (void *));
          unsigned size = *(unsigned *) validate_user_read_ptr (f->esp + 3 * sizeof (int *),
                                                               sizeof (unsigned));
          f->eax = syscall_write (fd, buffer, size);
          break;
        }
      case SYS_SEEK:
        {
          int fd = *(int *) validate_user_read_ptr (f->esp + sizeof (int *),
                                                    sizeof (int));
          unsigned position = *(unsigned *) validate_user_read_ptr (f->esp + 2 * sizeof (int *),
                                                                    sizeof (unsigned));
          syscall_seek (fd, position);
          break;
        }
      case SYS_TELL:
        {
          int fd = *(int *) validate_user_read_ptr (f->esp + sizeof (int *),
                                                    sizeof (int));
          f->eax = (uint32_t) syscall_tell (fd);
          break;
        }
      case SYS_CLOSE:
        {
          int fd = *(int *) validate_user_read_ptr (f->esp + sizeof (int *),
                                                    sizeof (int));
          syscall_close (fd);
          break;
        }
      case SYS_MMAP:
        {
          int fd = *(int *) validate_user_read_ptr (f->esp + sizeof (int *),
                                                    sizeof (int));
          void *addr = *(void **) validate_user_read_ptr (f->esp + 2 * sizeof (int *),
                                                         sizeof (void *));
          f->eax = (uint32_t) syscall_mmap (fd, addr);
          break;
        }
      case SYS_MUNMAP:
        {
          mapid_t mapping = *(mapid_t *) validate_user_read_ptr (f->esp + sizeof (int *),
                                                                sizeof (mapid_t));
          syscall_munmap (mapping);
          break;
        }
      default:
        PANIC("Unknown system call.");
    }
  process_unpin_all_frames (thread_current ());
}

static void
syscall_halt (void)
{
  shutdown_power_off ();
}

void
syscall_exit (int status)
{
  struct thread *cur = thread_current ();
  struct thread *parent = thread_get_by_tid (cur->parent_tid);
  cur->exit_code = status;

  if (parent != NULL && !list_empty(&parent->child_list))
    {
      for (struct list_elem *e = list_begin (&parent->child_list);
           e != list_end (&parent->child_list); e = list_next (e))
      {
        struct child_status *child = list_entry (e, struct child_status, child_elem);
        if (child->tid == cur->tid)
          {
            child->exit_code = status;
            sema_up (&child->wait_sema);
            break;
          }
      }
    }

  thread_exit ();
}

static tid_t
syscall_exec (const char *cmd_line)
{
  if (!validate_user_string (cmd_line, 129))
    return -1;
  return process_execute (cmd_line);
}

static int
syscall_wait (tid_t tid)
{
  return process_wait (tid);
}

static bool
syscall_create (const char *name, unsigned initial_size)
{
  if (!validate_user_string (name, 14))
    return false;
  lock_acquire (&filesys_lock);
  bool res = filesys_create (name, initial_size);
  lock_release (&filesys_lock);
  return res;
}

static bool
syscall_remove (const char *name)
{
  if (!validate_user_string (name, 14))
    return false;
  lock_acquire (&filesys_lock);
  bool res = filesys_remove (name);
  lock_release (&filesys_lock);
  return res;
}

static int
syscall_open (const char *name)
{
  if (!validate_user_string (name, 14))
    return -1;

  lock_acquire (&filesys_lock);
  struct file *file = filesys_open (name);
  lock_release (&filesys_lock);

  if (file == NULL)
    return -1;

  struct thread *cur = thread_current ();
  struct file_descriptor *fd = malloc (sizeof (struct file_descriptor));
  if (fd == NULL)
    return -1;

  fd->file = file;
  fd->fd = cur->fd_cnt++;
  list_push_back (&cur->fd_list, &fd->elem);

  return fd->fd;
}

static int
syscall_filesize (int fd)
{
  struct file_descriptor *fd_elem = get_file_by_fd (fd);
  if (fd_elem == NULL)
    return -1;

  lock_acquire (&filesys_lock);
  int size = file_length (fd_elem->file);
  lock_release (&filesys_lock);

  return size;
}

static int
syscall_read (int fd, void *buffer, unsigned size)
{
  validate_user_write_ptr (buffer, size);

  /* Read from stdin. */
  if (fd == STDIN_FILENO)
    {
      for (unsigned i = 0; i < size; i++)
        {
          *(uint8_t *) buffer = input_getc ();
          buffer += sizeof (uint8_t);
        }
      return size;
    }
  
  /* Invalid to read from stdout. */
  if (fd == STDOUT_FILENO)
    return -1;

  /* Read from opened file. */
  struct file_descriptor *file_desc = get_file_by_fd (fd);
  if (file_desc == NULL)
    return -1;
  lock_acquire (&filesys_lock);
  int bytes_read = file_read (file_desc->file, buffer, size);
  lock_release (&filesys_lock);
  return bytes_read;
}

static int
syscall_write (int fd, const void *buffer, unsigned size)
{
  validate_user_read_ptr(buffer, size);

  /* Write to stdout. */
  if (fd == STDOUT_FILENO)
    {
      putbuf (buffer, size);
      return size;
    }
  
  /* Invalid to write to stdin. */
  if (fd == STDIN_FILENO)
    return -1;

  /* Write to opened file. */
  struct file_descriptor *file_desc = get_file_by_fd (fd);
  if (file_desc == NULL)
    return -1;
  lock_acquire (&filesys_lock);
  int bytes_written = file_write (file_desc->file, buffer, size);
  lock_release (&filesys_lock);

  return bytes_written;
}

static void
syscall_seek (int fd, unsigned position)
{
  struct file_descriptor *file_desc = get_file_by_fd (fd);
  if (file_desc == NULL)
    return;
  lock_acquire (&filesys_lock);
  file_seek (file_desc->file, position);
  lock_release (&filesys_lock);
}

static unsigned
syscall_tell (int fd)
{
  struct file_descriptor *file_desc = get_file_by_fd (fd);
  if (file_desc == NULL)
    return (unsigned)-1;
  lock_acquire (&filesys_lock);
  unsigned pos = file_tell (file_desc->file);
  lock_release (&filesys_lock);
  return pos;
}

static void
syscall_close (int fd)
{
  struct file_descriptor *file_desc = get_file_by_fd (fd);
  if (file_desc == NULL)
    return;

  lock_acquire (&filesys_lock);
  file_close (file_desc->file);
  lock_release (&filesys_lock);

  list_remove (&file_desc->elem);
  free (file_desc);
}

static mapid_t
syscall_mmap (int fd, void *addr)
{
  /* Validate address. */
  if (addr == NULL || pg_ofs (addr) != 0)
    return -1;
  /* Validate fd. */
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO)
    return -1;

  /* Get file descriptor. */
  struct file_descriptor *file_desc = get_file_by_fd (fd);
  if (file_desc == NULL)
    return -1;

  mapid_t mapping = -1;

  lock_acquire (&filesys_lock);

  /* Reopen file */
  struct file *file = file_reopen (file_desc->file);
  if (file == NULL)
    goto done;
  
  /* Validate file size & mappeable memory size. */
  off_t file_size = file_length (file);
  if (file_size == 0)
    goto done;
  for (off_t i = 0; i < file_size; i += PGSIZE)
    {
      if (page_get_spte (addr + i) != NULL)
        goto done;
    }

  /* Map file tot memory. */
  for (off_t i = 0; i < file_size; i += PGSIZE)
    {
      struct sup_page_table_entry *spte = malloc (sizeof (struct sup_page_table_entry));
      spte->uaddr = addr + i;
      spte->kaddr = NULL;

      spte->writable = true;
      spte->type = PAGE_MMAP;

      spte->file = file;
      spte->offset = i;
      spte->read_bytes = i + PGSIZE < file_size ? PGSIZE : file_size - i;
      spte->zero_bytes = PGSIZE - spte->read_bytes;
      spte->swap_index = (size_t)-1;

      lock_init (&spte->spte_lock);
      
      hash_insert (&thread_current ()->sup_page_table, &spte->elem);
    }

  /* Add mapping to mmap list. */
  struct mmap_file *mf = malloc (sizeof (struct mmap_file));
  mf->file = file;
  mapping = mf->mapid = thread_current ()->mapid_cnt++;
  mf->base = addr;
  list_push_back (&thread_current ()->mmap_list, &mf->elem);

  done :
  lock_release (&filesys_lock);
  return mapping;
}

void
syscall_munmap (mapid_t mapping)
{
  struct thread *cur = thread_current ();
  struct mmap_file *mf = NULL;

  for (struct list_elem *e = list_begin (&cur->mmap_list);
       e != list_end (&cur->mmap_list); e = list_next (e))
    {
      mf = list_entry (e, struct mmap_file, elem);
      if (mf->mapid == mapping)
        break;
    }

  if (mf == NULL)
    return;

  /* Write back to file. */
  for (off_t i = 0; i < file_length (mf->file); i += PGSIZE)
    {
      load_page (mf->base + i, true);
      struct sup_page_table_entry *spte = page_get_spte (mf->base + i);
      if (pagedir_is_dirty (cur->pagedir, mf->base + i))
        {
          lock_acquire (&filesys_lock);
          file_seek (mf->file, i);
          file_write (mf->file, spte->kaddr, PGSIZE);
          lock_release (&filesys_lock);
        }
      free_page (mf->base + i);
    }

  list_remove (&mf->elem);
  lock_acquire (&filesys_lock);
  file_close (mf->file);
  lock_release (&filesys_lock);
  free (mf);
}
