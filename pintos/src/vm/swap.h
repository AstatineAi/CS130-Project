#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stddef.h>

void swap_init (void);
void swap_in (size_t swap_index, void *kpage);
size_t swap_out (void *kpage);
void swap_free_slot (size_t swap_index);

#endif /* vm/swap.h */