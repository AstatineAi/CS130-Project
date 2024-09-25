#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/malloc.h"

/* Partition that contains the file system. */
struct block *fs_device;

struct lock filesys_lock;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");
  
  cache_init ();

  inode_init ();
  free_map_init ();

  if (format) 
  {
    // printf("IN\n");
    do_format ();
  }

  free_map_open ();

  // lock_init(&filesys_lock);
  // lock_acquire(&filesys_lock);
  get_initial_thread() -> dir = dir_open_root();
  // if(!format){
  //   dir_output(dir_get_inode(parse_to_dir("//start/dir0/dir1")));
  // }
  // lock_release(&filesys_lock);
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
  cache_close ();
}

/* Creates an inode named NAME with the given INITIAL_SIZE.
   Will create a directory if IS_DIR is true, else just a file.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool is_dir) 
{
  // printf("%s START.\n", name);
  block_sector_t inode_sector = 0;
  char **_results = parse_to_path_and_file_name(name);
  struct dir* dir = NULL;
  struct inode* inode = NULL;
  if(_results == NULL || !strcmp(".", _results[1]) || !strcmp("..", _results[1])){
    if(_results != NULL)
    {
      free(_results[0]);
      free(_results[1]);
      free(_results);
    }
    return false;
  }
  dir = parse_to_dir(_results[0]);
  if(dir == NULL || dir_lookup(dir, _results[1], &inode))
  {
    // printf("IN %d %d %d\n", _results == NULL, dir == NULL, inode != NULL);
    if(dir != NULL)
      dir_close(dir);
    if(inode != NULL)
      inode_close(inode);
    if(_results != NULL)
    {
      free(_results[0]);
      free(_results[1]);
      free(_results);
    }
    return false;
  }
  inode_down_all_sema(dir_get_inode(dir));
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, is_dir)
                  && dir_add (dir, _results[1], inode_sector));
  // printf("SUCCESS?: %d\n", success);
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  inode_up_all_sema(dir_get_inode(dir));
  // printf("%s COMPLETE, can find sub %s in %s? %d\n", name, _results[1], _results[0], dir_lookup(dir, _results[1], &inode));
  if(inode != NULL)
    inode_close(inode);
  free(_results[0]);
  free(_results[1]);
  free(_results);
  dir_close (dir);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  if (!strcmp(name, "/"))
    return file_open(inode_open(ROOT_DIR_SECTOR));
  // printf("IN!");
  char **_results = parse_to_path_and_file_name(name);
  struct dir* dir = NULL;
  struct inode* inode = NULL;
  struct file* result = NULL;
  // printf("path: %d\n", !strcmp(_results[1], "."));
  if(_results == NULL)
    result = NULL;
  else
  {
    char *path = _results[0], *file_name = _results[1];
    dir = parse_to_dir(path);
    if (dir == NULL)
      result = NULL;
    else 
    {
      if (!strcmp(file_name, "."))
      {
        inode = inode_open(inode_get_inumber(dir_get_inode(dir)));
        // printf("check if is \"/a\": %d\n", inode_get_parent(inode) == ROOT_DIR_SECTOR);
      }
      else if (!strcmp(file_name, ".."))
        inode = inode_open(inode_get_parent(dir_get_inode(dir)));
      else
        dir_lookup(dir, _results[1], &inode);
      if (inode == NULL)
        result = NULL;
      else
        result = file_open(inode);
      dir_close (dir);
    }
    free(_results[0]);
    free(_results[1]);
    free(_results);
  }
  return result;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  if (!strcmp(name, "/"))
    return false;
  char **_results = parse_to_path_and_file_name(name);
  struct dir* dir = NULL;
  struct inode* inode = NULL;
  if(_results == NULL || (dir = parse_to_dir(_results[0])) == NULL || !dir_lookup(dir, _results[1], &inode)){
    if(dir != NULL)
      dir_close(dir);
    if(inode != NULL)
      inode_close(inode);
    if(_results != NULL)
    {
      free(_results[0]);
      free(_results[1]);
      free(_results);
    }
    return NULL;
  }
  inode_close(inode);
  // printf("dir == root: %d\n", inode_get_inumber(dir_get_inode(dir)) == ROOT_DIR_SECTOR);
  
  bool success = dir != NULL && dir_remove (dir, _results[1]);

  dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}