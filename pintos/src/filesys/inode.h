#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"

struct bitmap;

void inode_init (void);
bool inode_create (block_sector_t, off_t, bool);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (struct inode *);
bool inode_is_dir(struct inode *);
block_sector_t inode_get_parent(struct inode *);
bool inode_set_parent(block_sector_t idx, block_sector_t parent_index);
int inode_get_open_cnt(struct inode *);


void inode_down_reader_sema(struct inode*);
void inode_up_reader_sema(struct inode*);
void inode_down_writer_sema(struct inode*);
void inode_up_writer_sema(struct inode*);
void inode_down_all_sema(struct inode*);
void inode_up_all_sema(struct inode*);



struct semaphore *inode_get_all_mutex(struct inode*);
#endif /* filesys/inode.h */
