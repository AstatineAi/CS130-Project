#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include <stdio.h>
#include "devices/block.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"
#include "stdbool.h"
#include "threads/malloc.h"
#include "filesys/directory.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define INODE_DIRECT_BLOCKS 12
#define INODE_INDIRECT_PER_BLOCK 128
#define INODE_NO_SECTOR (block_sector_t)-1

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    bool is_dir;                        /* If the inode represents a directory. */
    block_sector_t parent;              /* The parent inode of the inode */
    block_sector_t direct[INODE_DIRECT_BLOCKS];          /* Direct blocks. */
    block_sector_t indirect_lv1;        /* Indirect block for level 1. */
    block_sector_t indirect_lv2;        /* Indirect block for level 2. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[110];               /* Not used. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    // start synch variables
    struct semaphore reader_mutex;           /* The mutal exclusion lock for readers. */
    struct semaphore writer_mutex;           /* The mutal exclusion lock for writers. */
    struct semaphore all_mutex;              /* The mutal exclusion lock for readers, writers and extend operations. */
    int reader_cnt;                     /* The count of readers. */
    int writer_cnt;                     /* The count of readers. */
    // end synch variables
    struct inode_disk data;             /* Inode content. */
  };

static block_sector_t double_indirect_lookup (block_sector_t sector, off_t pos);
static block_sector_t indirect_lookup (block_sector_t sector, off_t pos);

bool inode_create_indirect (block_sector_t *sector);
bool inode_extend_file (struct inode_disk *disk_inode, size_t sectors);
bool inode_extend_indirect (block_sector_t sector, size_t *sectors);

static block_sector_t
double_indirect_lookup (block_sector_t sector, off_t pos)
{
  int index = pos / BLOCK_SECTOR_SIZE / INODE_INDIRECT_PER_BLOCK;
  block_sector_t *double_indirect = malloc (BLOCK_SECTOR_SIZE);
  cache_read (sector, double_indirect);
  block_sector_t result = indirect_lookup (double_indirect[index],
                                           pos - index * INODE_INDIRECT_PER_BLOCK * BLOCK_SECTOR_SIZE);
  free (double_indirect);
  return result;
}

static block_sector_t
indirect_lookup (block_sector_t sector, off_t pos)
{
  int index = pos / BLOCK_SECTOR_SIZE;
  block_sector_t *indirect = malloc (BLOCK_SECTOR_SIZE);
  cache_read (sector, indirect);
  block_sector_t result = indirect[index];
  free (indirect);
  return result;
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos >= inode->data.length)
    return -1;
  /* Try look up in direct blocks. */
  if (pos < BLOCK_SECTOR_SIZE * INODE_DIRECT_BLOCKS)
    return inode->data.direct[pos / BLOCK_SECTOR_SIZE];

  /* Try look up in indirect block for level 1. */
  if (pos <   BLOCK_SECTOR_SIZE * INODE_DIRECT_BLOCKS
            + BLOCK_SECTOR_SIZE * INODE_INDIRECT_PER_BLOCK)
    return indirect_lookup (inode->data.indirect_lv1,
                            pos - BLOCK_SECTOR_SIZE * INODE_DIRECT_BLOCKS);
  
  /* Try look up in indirect block for level 2. */
  if (pos <   BLOCK_SECTOR_SIZE * INODE_DIRECT_BLOCKS
            + BLOCK_SECTOR_SIZE * INODE_INDIRECT_PER_BLOCK
            + BLOCK_SECTOR_SIZE * INODE_INDIRECT_PER_BLOCK * INODE_INDIRECT_PER_BLOCK)
    return double_indirect_lookup (inode->data.indirect_lv2,
                                   pos - BLOCK_SECTOR_SIZE * INODE_DIRECT_BLOCKS
                                           - BLOCK_SECTOR_SIZE * INODE_INDIRECT_PER_BLOCK);
  
  /* Impossible case. */
  return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

struct lock lock_open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  lock_init (&lock_open_inodes);
}

/* Create a new indirect block and initialize it with INODE_NO_SECTOR. */
bool
inode_create_indirect (block_sector_t *sector)
{
  if (free_map_allocate (1, sector))
    {
      block_sector_t *indirect = malloc (BLOCK_SECTOR_SIZE);
      for (int i = 0; i < INODE_INDIRECT_PER_BLOCK; i++)
        indirect[i] = INODE_NO_SECTOR;
      cache_write (*sector, indirect);
      free (indirect);
      return true;
    }
  return false;
}

/* Extend a single indirect block by SECTORS sectors. */
bool
inode_extend_indirect (block_sector_t sector, size_t *sectors)
{
  block_sector_t *indirect = malloc (BLOCK_SECTOR_SIZE);
  bool result = true;
  cache_read (sector, indirect);
  for (int i = 0; i < INODE_INDIRECT_PER_BLOCK && *sectors > 0; i++)
    {
      if (indirect[i] == INODE_NO_SECTOR)
        {
          if (free_map_allocate (1, &indirect[i]))
            {
              cache_set_zero (indirect[i]);
              (*sectors)--;
            }
          else
            {
              result = false;
              break;
            }
        }
    }
  cache_write (sector, indirect);
  free (indirect);
  return result;
}

/* Extends the file by SECTORS sectors. */
bool
inode_extend_file (struct inode_disk *disk_inode, size_t sectors)
{
  if (sectors == 0)
    return true;
  /* Check free direct blocks. */
  for (size_t i = 0; i < INODE_DIRECT_BLOCKS && sectors > 0; i++)
    {
      if (disk_inode->direct[i] == INODE_NO_SECTOR)
        {
          if (free_map_allocate (1, &disk_inode->direct[i]))
            {
              cache_set_zero (disk_inode->direct[i]);
              sectors--;
            }
          else
            return false;
        }
    }
  
  if (sectors == 0)
    return true;

  /* Check free sectors in single indirect block. */
  if (disk_inode->indirect_lv1 == INODE_NO_SECTOR)
    {
      if (!inode_create_indirect (&disk_inode->indirect_lv1))
        return false;
    }

  /* Try to allocate sectors in single indirect block. */
  if (!inode_extend_indirect (disk_inode->indirect_lv1, &sectors))
    return false;

  if (sectors == 0)
    return true;

  /* Check free sectors in double indirect block. */
  if (disk_inode->indirect_lv2 == INODE_NO_SECTOR)
    {
      if (!inode_create_indirect (&disk_inode->indirect_lv2))
        return false;
    }

  /* Try to allocate sectors in double indirect block. */
  block_sector_t *double_indirect = malloc (BLOCK_SECTOR_SIZE);
  cache_read (disk_inode->indirect_lv2, double_indirect);
  for (int i = 0; i < INODE_INDIRECT_PER_BLOCK && sectors > 0; i++)
    {
      if (double_indirect[i] == INODE_NO_SECTOR)
        {
          if (!inode_create_indirect (&double_indirect[i]))
            {
              free (double_indirect);
              return false;
            }
        }
      if (!inode_extend_indirect (double_indirect[i], &sectors))
        {
          free (double_indirect);
          return false;
        }
      if (sectors == 0)
        break;
    }
  cache_write (disk_inode->indirect_lv2, double_indirect);
  free (double_indirect);
  if (sectors != 0)
    PANIC ("Too large file size.");
  return true;
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   The inode represents a directory if IS_DIR is TRUE,
   otherwise a FILE
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      struct inode *inode = inode_open(sector);
      inode_down_all_sema(inode);
      size_t sectors = bytes_to_sectors (length);
      // printf("is dir ?%d\n", is_dir);
      disk_inode->is_dir = is_dir;
      disk_inode->parent = ROOT_DIR_SECTOR;
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      for (int i = 0; i < INODE_DIRECT_BLOCKS; i++)
        disk_inode->direct[i] = INODE_NO_SECTOR;
      disk_inode->indirect_lv1 = INODE_NO_SECTOR;
      disk_inode->indirect_lv2 = INODE_NO_SECTOR;
      if (inode_extend_file (disk_inode, sectors))
        {
          cache_write (sector, disk_inode);
          success = true; 
        }
      cache_read(inode->sector, &inode->data);
      inode_up_all_sema(inode);
      inode_close(inode);
      free (disk_inode);
    }
  return success;
}

/* Down the reader sema of a inode*/
void
inode_down_reader_sema(struct inode* inode)
{
  sema_down(&inode -> reader_mutex);
  inode -> reader_cnt ++;
  if (inode -> reader_cnt == 1)
  {
    sema_down(&inode -> all_mutex);
    // printf("Acquire all_mutex of inode %d in reader lock\n", inode_get_inumber(inode));
  }
    
  sema_up(&inode -> reader_mutex);
}

/* Up the reader sema of a inode*/
void
inode_up_reader_sema(struct inode* inode)
{
  sema_down(&inode -> reader_mutex);
  inode -> reader_cnt --;
  if (inode -> reader_cnt == 0)
  {
    sema_up(&inode -> all_mutex);
    // printf("Release all_mutex of inode %d in reader lock\n", inode_get_inumber(inode));

  }
  sema_up(&inode -> reader_mutex);
}

/* Down the writer sema of a inode*/
void
inode_down_writer_sema(struct inode* inode)
{
  sema_down(&inode -> writer_mutex);
  inode -> writer_cnt ++;
  if (inode -> writer_cnt == 1)
  {
    sema_down(&inode -> all_mutex);
    // printf("Acquire all_mutex of inode %d in writer lock\n", inode_get_inumber(inode));
  }
  sema_up(&inode -> writer_mutex);
}

/* Up the writer sema of a inode*/
void
inode_up_writer_sema(struct inode* inode)
{
  sema_down(&inode -> writer_mutex);
  inode -> writer_cnt --;
  if (inode -> writer_cnt == 0)
  {
    sema_up(&inode -> all_mutex);
    // printf("Release all_mutex of inode %d in writer lock\n", inode_get_inumber(inode));
  }
    
  sema_up(&inode -> writer_mutex);
}

/* Down the all sema of a inode*/
void
inode_down_all_sema(struct inode* inode)
{
  sema_down(&inode -> all_mutex);
}

/* Up the all sema of a inode*/

void
inode_up_all_sema(struct inode* inode)
{
  sema_up(&inode -> all_mutex);
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  lock_acquire (&lock_open_inodes);
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          lock_release (&lock_open_inodes);
          // printf("REASONABLE.\n");
          return inode; 
        }
    }
  lock_release (&lock_open_inodes);
  
  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  sema_init(&inode->reader_mutex, 1);
  sema_init(&inode->writer_mutex, 1);
  sema_init(&inode->all_mutex, 1);
  inode->reader_cnt = inode->writer_cnt = 0;
  inode->sector = sector;
  inode_down_writer_sema(inode);

  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  cache_read (inode->sector, &inode->data);

  inode_up_writer_sema(inode);
  
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
  {
    inode_down_writer_sema(inode);

    inode->open_cnt++;
    
    inode_up_writer_sema(inode);
  }
    
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (struct inode *inode)
{
  // inode_down_reader_sema(inode);

  block_sector_t result = inode->sector;

  // inode_up_reader_sema(inode);

  return result;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;
  inode_down_all_sema(inode);
  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      // printf("LAST!\n");
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          /* free direct blocks. */
          for (int i = 0; i < INODE_DIRECT_BLOCKS; i++)
            {
              if (inode->data.direct[i] != INODE_NO_SECTOR)
                free_map_release (inode->data.direct[i], 1);
            }
          /* free single indirect block. */
          if (inode->data.indirect_lv1 != INODE_NO_SECTOR)
            {
              block_sector_t *indirect = malloc (BLOCK_SECTOR_SIZE);
              cache_read (inode->data.indirect_lv1, indirect);
              for (int i = 0; i < INODE_INDIRECT_PER_BLOCK; i++)
                {
                  if (indirect[i] != INODE_NO_SECTOR)
                    free_map_release (indirect[i], 1);
                }
              free_map_release (inode->data.indirect_lv1, 1);
              free (indirect);
            }
          /* free double indirect block. */
          if (inode->data.indirect_lv2 != INODE_NO_SECTOR)
            {
              block_sector_t *double_indirect = malloc (BLOCK_SECTOR_SIZE);
              cache_read (inode->data.indirect_lv2, double_indirect);
              for (int i = 0; i < INODE_INDIRECT_PER_BLOCK; i++)
                {
                  if (double_indirect[i] != INODE_NO_SECTOR)
                    {
                      block_sector_t *indirect = malloc (BLOCK_SECTOR_SIZE);
                      cache_read (double_indirect[i], indirect);
                      for (int j = 0; j < INODE_INDIRECT_PER_BLOCK; j++)
                        {
                          if (indirect[j] != INODE_NO_SECTOR)
                            free_map_release (indirect[j], 1);
                        }
                      free_map_release (double_indirect[i], 1);
                      free (indirect);
                    }
                }
              free_map_release (inode->data.indirect_lv2, 1);
              free (double_indirect);
            }
          free_map_release (inode->sector, 1);
        }
      inode_up_all_sema(inode);
      free (inode); 
    }
  else inode_up_all_sema(inode);
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode_down_writer_sema(inode);
  inode->removed = true;
  inode_up_writer_sema(inode);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  // inode_down_reader_sema(inode);
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      /* If sector_idx is -1, it means that we are reading beyond EOF. */
      if (sector_idx == INODE_NO_SECTOR)
        break;
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          cache_read (sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          cache_read (sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  // inode_up_reader_sema(inode);
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;
  bool need_extend_write = false; // for two different types of semaphore operations
  if (inode->deny_write_cnt)
    return 0;

  /* If writing beyond EOF, extend the file. */
  if (offset + size > inode->data.length)
    {
      need_extend_write = true;
      struct inode_disk *disk_inode = &inode->data;
      size_t sectors = bytes_to_sectors (offset + size) - bytes_to_sectors (disk_inode->length);
      if (!inode_extend_file (disk_inode, sectors))
        return 0;
      disk_inode->length = offset + size;
      // printf("EXTENDING DIR %d\n", inode->sector);
      cache_write (inode->sector, disk_inode);
      // printf("000\n");
      // dir_output(inode);
      // cache_read (inode->sector, disk_inode);
      // printf("101\n");
      // dir_output(inode);
      // printf("EXTENDED DIR %d\n", inode->sector);
    }
  else ++inode->writer_cnt; // pretend it is a normal operation.
  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          cache_write (sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            cache_read (sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          cache_write (sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  // if(need_extend_write)
  // {
  //   printf("111 %d\n", inode->data.length);
  //   dir_output(inode);
  // }
    

  // cache_read(inode->sector, &inode->data);
  
  if (!need_extend_write){
    if(--inode->writer_cnt == 0)
      inode_up_all_sema(inode);
  }
  // else
  // {
  //   printf("222 %d\n", inode->data.length);
  //   dir_output(inode);
  // }
    
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode_down_writer_sema(inode);
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode_up_writer_sema(inode);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  inode_down_writer_sema(inode);
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  inode_up_writer_sema(inode);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (struct inode *inode)
{
  // inode_down_reader_sema(inode);
  off_t result = inode->data.length;
  // inode_up_reader_sema(inode);
  return result;
}

/* Returns whether the inode is a directory */
bool
inode_is_dir (struct inode *inode)
{
  // printf("Attempting to fetch is dir of inode %d\n", inode->sector);
  inode_down_reader_sema(inode);

  bool result = inode->data.is_dir;

  inode_up_reader_sema(inode);

  return result;
}

/* Get the parent sector index of the current inode. */
block_sector_t
inode_get_parent(struct inode *inode)
{
  block_sector_t result = inode -> data.parent;
  return result;
}


/*
  Set a parent for the disk inode at IDX to PARENT_INDEX.
*/
bool
inode_set_parent(block_sector_t idx, block_sector_t parent_index)
{
  struct inode *inode = inode_open(idx);
  if (inode == NULL)
    return false;
  inode_down_writer_sema(inode);
  struct inode_disk *disk_inode = &inode->data;
  disk_inode -> parent = parent_index;
  cache_write (inode->sector, disk_inode);
  inode_up_writer_sema(inode);
  inode_close(inode);
  return true;
}

/* Get the user count of the INODE*/
int
inode_get_open_cnt(struct inode *inode)
{
  inode_down_reader_sema(inode);

  int result = inode -> open_cnt;

  inode_up_reader_sema(inode);

  return result;
}


struct semaphore *
inode_get_all_mutex(struct inode* inode)
{
  return &inode -> all_mutex;
}
