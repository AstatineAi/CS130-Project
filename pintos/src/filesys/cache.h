#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "threads/synch.h"

#include <stdbool.h>
#include <stddef.h>

#define WRITE_BEHIND_INTERVAL 1000
#define CACHE_SIZE ((int8_t)64)
#define NO_CACHING ((int8_t)-1)
#define NO_CACHING_SECTOR (block_sector_t)-1

extern struct lock cache_lock;

struct cache_block
  {
    bool dirty;                      /* If the block has been modified. */
    bool accessed;                   /* If the block has been accessed recently. */
    bool valid;                      /* If the block is valid. */
    block_sector_t sector;           /* The sector number of the block. */
    uint8_t data[BLOCK_SECTOR_SIZE]; /* The data of the block. */
  };

void cache_init (void);
void cache_close (void);
void cache_read (block_sector_t, void *);
void cache_write (block_sector_t, const void *);
void cache_set_zero (block_sector_t);

#endif /* FILESYS_CACHE_H */
