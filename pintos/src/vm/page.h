#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stddef.h>
#include <hash.h>
#include <debug.h>
#include "threads/synch.h"
#include "filesys/file.h"

#define MAX_STACK_SIZE (1 << 22)  // 4 MB

enum sup_page_type
  {
    PAGE_ZERO,    // All zero page
    PAGE_FILE,    // Page from file
    PAGE_STACK,   // Page on frame
    PAGE_MMAP     // Memory mapped file
  };

struct sup_page_table_entry
  {
    void *uaddr;              // User virtual address
    void *kaddr;              // Kernel virtual address

    bool writable;            // Is page writable
    enum sup_page_type type;  // Type of page

    struct lock spte_lock;    // Lock for page (wait for swapping to be done)

    struct file *file;        // File to load page from
    off_t offset;             // Offset in file
    uint32_t read_bytes;      // Number of bytes to read
    uint32_t zero_bytes;      // Number of bytes to zero

    struct hash_elem elem;    // Hash element for supplementary page table

    size_t swap_index;        // Swap index
  };

hash_less_func spte_less;
hash_hash_func spte_hash;
hash_action_func process_free_page;

void *page_get_spte (const void *uaddr);

bool lazy_load_file_page (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t page_read_bytes, uint32_t page_zero_bytes, bool writable);
bool load_page (void *fault_addr, bool pin);
void free_page (void *uaddr);
bool stack_grow (void *fault_addr, bool pin);

void process_free_all_pages (struct hash *spt);
void process_free_mmap_files (struct list *mmap_list);

#endif /* vm/page.h */