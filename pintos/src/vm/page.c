#include "vm/page.h"
#include "stdbool.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "filesys/filesys.h"

#include <stdio.h>
#include <string.h>

bool
spte_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED)
{
  const struct sup_page_table_entry *a = hash_entry (a_, struct sup_page_table_entry, elem);
  const struct sup_page_table_entry *b = hash_entry (b_, struct sup_page_table_entry, elem);

  return a->uaddr < b->uaddr;
}

unsigned
spte_hash (const struct hash_elem *e_, void *aux UNUSED)
{
  const struct sup_page_table_entry *e = hash_entry (e_, struct sup_page_table_entry, elem);

  return hash_bytes (&e->uaddr, sizeof(e->uaddr));
}

void *
page_get_spte (const void *uaddr)
{
  struct sup_page_table_entry spte_;
  spte_.uaddr = pg_round_down (uaddr);

  struct hash_elem *e = hash_find (&thread_current ()->sup_page_table, &spte_.elem);
  if (e == NULL)
    return NULL;

  return hash_entry (e, struct sup_page_table_entry, elem);
}

/* Lazy loads a page from file. 
   This function do not insert the page into page directory,
   so a page fault may occur and the page will be load then. */
bool
lazy_load_file_page (struct file *file, off_t ofs, uint8_t *upage,
                     uint32_t page_read_bytes, uint32_t page_zero_bytes, bool writable)
{
  struct sup_page_table_entry *spte = malloc (sizeof (struct sup_page_table_entry));
  if (spte == NULL)
    return false;
  
  spte->uaddr = upage;
  spte->kaddr = NULL;

  spte->writable = writable;
  if (page_read_bytes == 0)
    spte->type = PAGE_ZERO;
  else
    spte->type = PAGE_FILE;

  lock_init (&spte->spte_lock);
  
  spte->file = file;
  spte->offset = ofs;
  spte->read_bytes = page_read_bytes;
  spte->zero_bytes = page_zero_bytes;

  hash_insert (&thread_current ()->sup_page_table, &spte->elem);

  spte->swap_index = (size_t)-1;

  return true;
}

/* Load a page from swap or file. */
bool
load_page (void *fault_addr, bool pin)
{
  ASSERT (fault_addr != NULL);

  struct sup_page_table_entry *spte = page_get_spte (fault_addr);

  if (spte == NULL)
    return false;
  
  lock_acquire (&spte->spte_lock);

  if (spte->kaddr != NULL)
    {
      if (pin)
        frame_pin (spte->kaddr);
      lock_release (&spte->spte_lock);
      return true;
    }

  if (spte->swap_index != (size_t)-1)
    {
      spte->kaddr = frame_alloc (spte, PAL_USER);
      if (spte->kaddr == NULL)
          return false;
      swap_in (spte->swap_index, spte->kaddr);
      spte->swap_index = (size_t)-1;
    }
  else
    {
      switch (spte->type)
        {
          case PAGE_ZERO:
            {
              spte->kaddr = frame_alloc (spte, PAL_USER | PAL_ZERO);
              if (spte->kaddr == NULL)
                {
                  lock_release (&spte->spte_lock);
                  return false;
                }
              break;
            }
          case PAGE_FILE:
            {
              spte->kaddr = frame_alloc (spte, PAL_USER);
              if (spte->kaddr != NULL)
                {
                  lock_acquire (&filesys_lock);
                  file_seek (spte->file, spte->offset);
                  if (file_read (spte->file, spte->kaddr, spte->read_bytes) != (int) spte->read_bytes)
                    {
                      lock_release (&filesys_lock);
                      lock_release (&spte->spte_lock);
                      frame_free (spte->kaddr);
                      return false;
                    }
                  lock_release (&filesys_lock);
                  memset (spte->kaddr + spte->read_bytes, 0, spte->zero_bytes);
                }
              else
                {
                  lock_release (&spte->spte_lock);
                  return false;
                }
              break;
            }
          case PAGE_STACK:
            break;
          case PAGE_MMAP:
            {
              spte->kaddr = frame_alloc (spte, PAL_USER);
              if (spte->kaddr != NULL)
                {
                  lock_acquire (&filesys_lock);
                  file_seek (spte->file, spte->offset);
                  if (file_read (spte->file, spte->kaddr, spte->read_bytes) != (int) spte->read_bytes)
                    {
                      lock_release (&filesys_lock);
                      lock_release (&spte->spte_lock);
                      frame_free (spte->kaddr);
                      return false;
                    }
                  lock_release (&filesys_lock);
                  memset (spte->kaddr + spte->read_bytes, 0, spte->zero_bytes);
                }
              else
                {
                  lock_release (&spte->spte_lock);
                  return false;
                }
              break;
            }
            break;
          default:
            NOT_REACHED ();
        }
    }

  if (!install_page (spte->uaddr, spte->kaddr, spte->writable))
    {
      lock_release (&spte->spte_lock);
      frame_free (spte->kaddr);
      return false;
    }
  
  // pagedir_set_dirty(thread_current ()->pagedir, spte->uaddr, false);

  if (!pin)
    frame_unpin (spte->kaddr);

  lock_release (&spte->spte_lock);

  return true;
}

void
free_page (void *uaddr)
{
  struct sup_page_table_entry *spte = page_get_spte (uaddr);
  if (spte == NULL)
    return;

  if (spte->swap_index != (size_t)-1)
    swap_free_slot (spte->swap_index);
  else if (spte->kaddr != NULL)
    frame_free (spte->kaddr);

  hash_delete (&thread_current ()->sup_page_table, &spte->elem);
  uint32_t *pd = thread_current ()->pagedir;
  pagedir_clear_page (pd, spte->uaddr);
  pagedir_set_dirty (pd, spte->uaddr, false);
  pagedir_set_accessed (pd, spte->uaddr, false);
  free (spte);
}

void
process_free_page (struct hash_elem *e, void *aux UNUSED)
{
  struct sup_page_table_entry *spte = hash_entry (e, struct sup_page_table_entry, elem);
  if (spte->swap_index != (size_t)-1)
    swap_free_slot (spte->swap_index);
  free (spte);
}

bool
stack_grow(void *fault_addr, bool pin)
{
  ASSERT (fault_addr != NULL);

  struct sup_page_table_entry *spte = malloc (sizeof (struct sup_page_table_entry));
  if (spte == NULL)
    return false;

  spte->uaddr = pg_round_down (fault_addr);
  spte->kaddr = frame_alloc (spte, PAL_USER);

  if (spte->kaddr == NULL)
    {
      free (spte);
      return false;
    }

  spte->writable = true;
  spte->type = PAGE_STACK;

  lock_init (&spte->spte_lock);

  spte->file = NULL;
  spte->offset = 0;
  spte->read_bytes = 0;
  spte->zero_bytes = PGSIZE;

  spte->swap_index = (size_t)-1;

  if (!install_page (spte->uaddr, spte->kaddr, true))
    {
      frame_free (spte->kaddr);
      free (spte);
      return false;
    }

  hash_insert (&thread_current ()->sup_page_table, &spte->elem);

  pagedir_set_dirty (thread_current ()->pagedir, spte->uaddr, true);
  if (!pin)
    frame_unpin (spte->kaddr);

  return true;
}

void
process_free_all_pages (struct hash *spt)
{
  hash_destroy (spt, process_free_page);
}

void
process_free_mmap_files(struct list *mmap_list)
{
  while (!list_empty (mmap_list))
    {
      struct list_elem *e = list_begin (mmap_list);
      struct mmap_file *mmap_file = list_entry (e, struct mmap_file, elem);
      syscall_munmap (mmap_file->mapid);
    }
}
