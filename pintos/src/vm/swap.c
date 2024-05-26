#include "vm/swap.h"

#include <bitmap.h>
#include <debug.h>
#include <stdio.h>
#include "devices/block.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/* Swap block device. */
struct block* swap_block;

/* Swap bitmap. */
struct bitmap* swap_bitmap;

/* Swapping lock. */
struct lock swap_lock;

/* Number of sectors per page. */
#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

/* Initalize swapping. */
void
swap_init (void)
{
  /* Get swap block device. */
  swap_block = block_get_role (BLOCK_SWAP);
  if (swap_block == NULL)
    PANIC ("No swap block device found.");

  /* Initialize swap bitmap. */
  swap_bitmap = bitmap_create (block_size (swap_block) / SECTORS_PER_PAGE);
  if (swap_bitmap == NULL)
    PANIC ("Failed to create swap bitmap.");

  /* Set all swap slots as free to use. */
  bitmap_set_all(swap_bitmap, false);

  lock_init (&swap_lock);
}

/* Swap in a page from swap block to page. */
void
swap_in (size_t swap_index, void *page)
{
  lock_acquire (&swap_lock);

  ASSERT (swap_index < bitmap_size (swap_bitmap));
  ASSERT (bitmap_test (swap_bitmap, swap_index));

  /* Read data from swap block to page. */
  for (size_t i = 0; i < SECTORS_PER_PAGE; i++)
    block_read (swap_block, swap_index * SECTORS_PER_PAGE + i,
                            page + i * BLOCK_SECTOR_SIZE);
  bitmap_set (swap_bitmap, swap_index, false);

  lock_release (&swap_lock);
}

/* Swap out a page from kpage to swap block. */
size_t
swap_out (void *page)
{
  lock_acquire (&swap_lock);

  /* Find a free swap slot. */
  size_t swap_index = bitmap_scan_and_flip (swap_bitmap, 0, 1, false);
  if (swap_index == BITMAP_ERROR)
    {
      lock_release (&swap_lock);
      return (size_t) -1;
    }

  /* Write data from page to swap block. */
  for (size_t i = 0; i < SECTORS_PER_PAGE; i++)
    block_write (swap_block, swap_index * SECTORS_PER_PAGE + i,
                             page + i * BLOCK_SECTOR_SIZE);

  lock_release (&swap_lock);

  return swap_index;
}

/* Free a swap slot.
   When a process exits, it should free all its swap slots. */
void
swap_free_slot (size_t swap_index)
{
  lock_acquire (&swap_lock);

  ASSERT (swap_index < bitmap_size (swap_bitmap));
  ASSERT (bitmap_test (swap_bitmap, swap_index) == true);

  bitmap_set (swap_bitmap, swap_index, false);

  lock_release (&swap_lock);
}
