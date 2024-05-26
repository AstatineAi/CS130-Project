#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stddef.h>
#include <list.h>
#include "threads/thread.h"
#include "threads/palloc.h"
#include "vm/page.h"

struct frame_table_entry
  {
    void *kaddr;                        // Kernel address
    struct sup_page_table_entry *spte;  // Supplementary page table entry
    struct thread* owner;               // Owner of the frame
    struct list_elem elem;              // List element for list of all frames
    bool pinned;                        // Is frame pinned
  };

void frame_init (void);
void *frame_alloc (struct sup_page_table_entry *spte, enum palloc_flags flags);
void frame_free (void *kaddr);

void process_free_all_frames (struct thread *t);
void process_unpin_all_frames (struct thread *t);

void frame_pin (void *kaddr);
void frame_unpin (void *kaddr);

#endif /* vm/frame.h */