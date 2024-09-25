#include "filesys/cache.h"
#include "devices/block.h"
#include "devices/timer.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <debug.h>
#include <string.h>

/* Cache buffer */
struct cache_block cache[CACHE_SIZE];
struct lock cache_lock;
typedef int8_t cache_index_t;

static cache_index_t clock_hand = 0;

thread_func read_ahead;
void write_behind (void *aux);

cache_index_t find_cache (block_sector_t);

cache_index_t evict_block_cache (void);
cache_index_t alloc_block_cache (void);
void free_block_cache (cache_index_t);

void
cache_init (void)
{
  lock_init (&cache_lock);
  for (cache_index_t i = 0; i < CACHE_SIZE; i++)
  {
      cache[i].dirty = false;
      cache[i].accessed = false;
      cache[i].valid = true;
      cache[i].sector = NO_CACHING_SECTOR;
  }
  thread_create("write-behind", PRI_DEFAULT, write_behind, NULL);
}

/* Periodical write-behind function */
void
write_behind (void *aux UNUSED)
{
  while (true)
    {
      timer_sleep(WRITE_BEHIND_INTERVAL);
      for (cache_index_t i = 0; i < CACHE_SIZE; i++)
        {
          lock_acquire(&cache_lock);
          if (cache[i].dirty && cache[i].sector != NO_CACHING_SECTOR)
            {
              block_write(fs_device, cache[i].sector, cache[i].data);
              cache[i].dirty = false;
            }
          lock_release(&cache_lock);
        }
    }
}

/* Read ahead function. Should be called asynchronously.
   Create a new thread to read ahead. */
void
read_ahead (void *sector_)
{
  block_sector_t sector = (block_sector_t) sector_;
  /* Check if the sector is already allocated in disk */
  if (sector >= block_size(fs_device) || !free_map_test(sector))
      return;
  lock_acquire(&cache_lock);
  cache_index_t cache_idx = find_cache(sector);
  if (cache_idx == NO_CACHING)
    {
      cache_idx = alloc_block_cache();
      block_read(fs_device, sector, cache[cache_idx].data);
      cache[cache_idx].sector = sector;
      cache[cache_idx].valid = false;
      cache[cache_idx].dirty = false;
    }
  lock_release(&cache_lock);
}

void
cache_read (block_sector_t sector, void *buffer)
{
  /* Get data from block or cache. */
  lock_acquire(&cache_lock);
  cache_index_t cache_idx = find_cache(sector);
  if (cache_idx == NO_CACHING)
    {
      /* Cache miss, read from disk */
      cache_idx = alloc_block_cache();
      block_read(fs_device, sector, cache[cache_idx].data);
      cache[cache_idx].sector = sector;
      cache[cache_idx].valid = false;
      cache[cache_idx].dirty = false;
    }
  
  /* Copy data to the buffer */
  memcpy(buffer, cache[cache_idx].data, BLOCK_SECTOR_SIZE);
  cache[cache_idx].accessed = true;
  
  lock_release(&cache_lock);

  /* Read ahead optimization */
  // thread_create ("read-ahead", PRI_DEFAULT, read_ahead, (void *) (sector + 1));
}

/* Write data to cache */
void
cache_write (block_sector_t sector, const void *buffer)
{
  lock_acquire(&cache_lock);
  cache_index_t cache_idx = find_cache(sector);
  if (cache_idx == NO_CACHING)
    {
      cache_idx = alloc_block_cache();
      block_read (fs_device, sector, cache[cache_idx].data);
      cache[cache_idx].sector = sector;
      cache[cache_idx].valid = false;
    }
  memcpy(cache[cache_idx].data, buffer, BLOCK_SECTOR_SIZE);
  cache[cache_idx].dirty = true;
  cache[cache_idx].accessed = true;
  lock_release(&cache_lock);
}

void
cache_set_zero (block_sector_t sector)
{
  lock_acquire(&cache_lock);
  cache_index_t cache_idx = find_cache(sector);
  if (cache_idx == NO_CACHING)
    {
      cache_idx = alloc_block_cache();
      block_read (fs_device, sector, cache[cache_idx].data);
      cache[cache_idx].sector = sector;
      cache[cache_idx].valid = false;
    }
  memset (cache[cache_idx].data, 0, BLOCK_SECTOR_SIZE);
  cache[cache_idx].dirty = true;
  cache[cache_idx].accessed = true;
  lock_release(&cache_lock);
}

/* Evict a cache block, second chance algorithm */
cache_index_t
evict_block_cache (void)
{
  ASSERT (lock_held_by_current_thread (&cache_lock));
  cache_index_t sector = NO_CACHING;
  while (true)
    {
      if (!cache[clock_hand].accessed)
        {
          sector = clock_hand;
          break;
        }
      else
        {
          cache[clock_hand].accessed = false;
          clock_hand = (clock_hand + 1) % CACHE_SIZE;
        }
    }
  
  /* Write back the block if it is dirty */
  if (cache[sector].dirty)
      block_write(fs_device, cache[sector].sector, cache[sector].data);
  
  return sector;
}

/* Get a free cache block or evict one */
cache_index_t
alloc_block_cache (void)
{
  /* Try finding a free block */
  ASSERT (lock_held_by_current_thread (&cache_lock));
  cache_index_t sector = NO_CACHING;
  for (cache_index_t i = 0; i < CACHE_SIZE; i++)
    {
      if (cache[i].valid)
        {
          sector = i;
          break;
        }
    }

  /* No free block, evict one */
  if (sector == NO_CACHING)
      sector = evict_block_cache();
  return sector;
}

/* Free a cache block */
void
free_block_cache (cache_index_t sector)
{
  lock_acquire(&cache_lock);
  if (cache[sector].dirty)
    {
      block_write(fs_device, cache[sector].sector, cache[sector].data);
      cache[sector].dirty = false;
    }
  cache[sector].accessed = false;
  cache[sector].valid = true;
  cache[sector].sector = NO_CACHING_SECTOR;
  lock_release(&cache_lock);
}

/* Find a cache block by sector */
cache_index_t
find_cache (block_sector_t sector)
{
  ASSERT (lock_held_by_current_thread (&cache_lock));
  cache_index_t cache_sector = NO_CACHING;
  for (cache_index_t i = 0; i < CACHE_SIZE; i++)
    {
      if (cache[i].sector == sector)
        {
          cache_sector = i;
          break;
        }
    }
  return cache_sector;
}

/* Close the cache, only called when the file system is shutting down */
void
cache_close (void)
{
  for (cache_index_t i = 0; i < CACHE_SIZE; i++)
    {
      if (cache[i].dirty && cache[i].sector != NO_CACHING_SECTOR)
          block_write(fs_device, cache[i].sector, cache[i].data);
    }
}
