#include "vm/frame.h"

#include <string.h>
#include "stddef.h"
#include "threads/palloc.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/interrupt.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

#include <stdio.h>

/* List of all frames allocated. */
struct list frame_table;

/* Lock for frame table. */
struct lock frame_lock;

/* Clock hand for clock algorithm. */
struct list_elem *clock_hand;

struct frame_table_entry* get_evict_frame (void);
void *evict_frame (void);

/* Initialize the frame table. */
void
frame_init (void)
{
  list_init (&frame_table);
  lock_init (&frame_lock);
  clock_hand = list_begin (&frame_table);
}

/* Get evict frame using clock algorithm. */
struct frame_table_entry*
get_evict_frame (void)
{
  struct frame_table_entry *fte = NULL;

  bool all_pinned = true;
  struct list_elem *e;
  for (e = list_begin (&frame_table); e != list_end (&frame_table);
       e = list_next (e))
    {
      fte = list_entry (e, struct frame_table_entry, elem);
      if (!fte->pinned)
        {
          all_pinned = false;
          break;
        }
    }
  
  if (all_pinned)
      return NULL;

  while (true)
    {
      /* Make the list circular. */
      if (clock_hand == list_end (&frame_table))
        clock_hand = list_begin (&frame_table);

      fte = list_entry (clock_hand, struct frame_table_entry, elem);
      clock_hand = list_next (clock_hand);

      if (!fte->pinned)
        {
          if (pagedir_is_accessed (fte->owner->pagedir, fte->spte->uaddr))
            {
              pagedir_set_accessed (fte->owner->pagedir,
                                    fte->spte->uaddr, false);
            }
          else
            {
              /* A frame should not be evicted simultaneously
                 by multiple threads. */
              lock_acquire (&fte->spte->spte_lock);
              enum intr_level old_level = intr_disable ();
              fte->spte->kaddr = NULL;
              pagedir_clear_page (fte->owner->pagedir, fte->spte->uaddr);
              intr_set_level (old_level);

              list_remove (&fte->elem);
              break;
            }
        }
    }
  return fte;
}

/* Get a frame to evict, then evict it and return kernel address. */
void *
evict_frame (void)
{
  struct frame_table_entry *fte = get_evict_frame ();

  if (fte == NULL)
    return NULL;
  
  if (fte->spte->type == PAGE_MMAP &&
      pagedir_is_dirty (fte->owner->pagedir, fte->spte->uaddr))
    {
      struct file *file = fte->spte->file;
      off_t offset = fte->spte->offset;
      lock_acquire (&filesys_lock);
      file_seek (file, offset);
      file_write (file, fte->kaddr, PGSIZE);
      lock_release (&filesys_lock);
      pagedir_set_dirty (fte->owner->pagedir, fte->spte->uaddr, false);
    }
  else
    {
      fte->spte->swap_index = swap_out (fte->kaddr); 
    }
  lock_release (&fte->spte->spte_lock);
  void *kaddr = fte->kaddr;
  free (fte);

  return kaddr;
}

/* Allocate a frame for the given user page UPAGE
   and supplementary page table entry SPTE.
   This frame should be inserted into the page table
   using install_page() after this function returns. */
void *
frame_alloc (struct sup_page_table_entry *spte, enum palloc_flags flags)
{
  lock_acquire (&frame_lock);
  /* Try allocating a new frame before running out of memory. */
  void *kaddr = palloc_get_page (flags);
  if (kaddr == NULL)
    {
      kaddr = evict_frame ();
      if (kaddr == NULL)
        {
          if (flags & PAL_ASSERT)
            PANIC ("frame_alloc: out of memory and eviction failed");
          return NULL;
        }
      if (flags & PAL_ZERO)
        memset (kaddr, 0, PGSIZE);
    }

  struct frame_table_entry *fte = malloc (sizeof (struct frame_table_entry));

  if (fte == NULL)
    {
      palloc_free_page (kaddr);
      return NULL;
    }

  fte->kaddr = kaddr;
  fte->spte = spte;
  fte->owner = thread_current ();
  fte->pinned = true;

  list_push_back (&frame_table, &fte->elem);
  lock_release (&frame_lock);

  return kaddr;
}

/* Free the frame that KADDR points to.
   kaddr should be removed from the page table
   before or after calling this function. (with spte lock) */
void
frame_free (void *kaddr)
{
  struct list_elem *e;
  struct frame_table_entry *fte = NULL;

  lock_acquire (&frame_lock);
  for (e = list_begin (&frame_table); e != list_end (&frame_table);
       e = list_next (e))
    {
      fte = list_entry (e, struct frame_table_entry, elem);
      if (fte->kaddr == kaddr)
        break;
    }

  if (fte == NULL)
    {
      lock_release (&frame_lock);
      return;
    };

  if (clock_hand == &fte->elem)
    clock_hand = list_next (clock_hand);

  list_remove (&fte->elem);
  palloc_free_page (kaddr);
  free (fte);

  lock_release (&frame_lock);
}

/* Free all frames that the given thread T owns. */
void
process_free_all_frames (struct thread *t)
{
  struct list_elem *e, *next;
  struct frame_table_entry *fte = NULL;
  

  lock_acquire (&frame_lock);
  for (e = list_begin (&frame_table); e != list_end (&frame_table);
       e = next)
    {
      next = list_next (e);
      fte = list_entry (e, struct frame_table_entry, elem);
      if (fte->owner == t)
        {
          if (clock_hand == &fte->elem)
            clock_hand = next;
          e = list_remove (&fte->elem);
          free (fte);
        }
    }
  lock_release (&frame_lock);
}

/* Pin the frame that KADDR points to. */
void
frame_pin (void *kaddr)
{
  struct list_elem *e;
  struct frame_table_entry *fte = NULL;

  lock_acquire (&frame_lock);
  for (e = list_begin (&frame_table); e != list_end (&frame_table);
       e = list_next (e))
    {
      fte = list_entry (e, struct frame_table_entry, elem);
      if (fte->kaddr == kaddr)
        break;
    }

  if (fte == NULL)
    {
      lock_release (&frame_lock);
      return;
    };

  fte->pinned = true;
  lock_release (&frame_lock);
}

/* Unpin the frame that KADDR points to. */
void
frame_unpin (void *kaddr)
{
  struct list_elem *e;
  struct frame_table_entry *fte = NULL;

  lock_acquire (&frame_lock);
  for (e = list_begin (&frame_table); e != list_end (&frame_table);
       e = list_next (e))
    {
      fte = list_entry (e, struct frame_table_entry, elem);
      if (fte->kaddr == kaddr)
        break;
    }

  if (fte == NULL)
    {
      lock_release (&frame_lock);
      return;
    };

  fte->pinned = false;
  lock_release (&frame_lock);
}

/* Unpin all the frames that the thread owns. */
void
process_unpin_all_frames (struct thread *t)
{
  struct list_elem *e;
  struct frame_table_entry *fte = NULL;

  lock_acquire (&frame_lock);
  for (e = list_begin (&frame_table); e != list_end (&frame_table);
       e = list_next (e))
    {
      fte = list_entry (e, struct frame_table_entry, elem);
      if (fte->owner == t)
        fte->pinned = false;
    }
  lock_release (&frame_lock);
}