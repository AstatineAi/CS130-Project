#ifndef FILESYS_DIRECTORY_H
#define FILESYS_DIRECTORY_H

#include <stdbool.h>
#include <stddef.h>
#include "devices/block.h"
#include "filesys/file.h"

/* Maximum length of a file name component.
   This is the traditional UNIX maximum length.
   After directories are implemented, this maximum length may be
   retained, but much longer full path names must be allowed. */
#define NAME_MAX 14

struct inode;

/* Parsing a directory name to struct dir. */
struct dir *parse_to_dir(const char*);

/* Get parent of a dir. */
struct dir *dir_open_parent (struct inode *);

/* Opening and closing directories. */
bool dir_create (block_sector_t sector, size_t entry_cnt);
struct dir *dir_open (struct inode *);
struct dir *dir_open_root (void);
struct dir *dir_reopen (struct dir *);
void dir_close (struct dir *);
struct inode *dir_get_inode (struct dir *);

/* Reading and writing. */
bool dir_lookup (struct dir *, const char *name, struct inode **);
bool dir_add (struct dir *, const char *name, block_sector_t);
bool dir_remove (struct dir *, const char *name);
bool dir_readdir (struct dir *, char name[NAME_MAX + 1]);

/* Whether the INODE is an empty dir*/
bool dir_is_empty(struct inode *inode);

struct dir * parse_to_dir(const char* name);
char** parse_to_path_and_file_name(const char* name);
void dir_output(struct inode *inode);
struct dir * dir_open_from_file (struct file *file);
void dir_close_to_file (struct dir* dir, struct file *file);
#endif /* filesys/directory.h */
