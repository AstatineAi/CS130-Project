#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "filesys/file.h"

/* A directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

/* 
  Parse a path NAME to a struct dir.
  Requires the user to close the dir.
  Returns NULL on failure;
*/
struct dir *
parse_to_dir(const char* _name)
{
  // printf("%s\n", _name);
  size_t len = strlen(_name);
  char* name = malloc((len + 1) * sizeof(char));
  strlcpy(name, _name, len + 1);
  struct dir *cur_dir=NULL, *prev_dir=NULL, *next_dir=NULL;
  struct inode *next_inode=NULL;
  char* dir_name, *save_ptr;
  dir_name = strtok_r (name, "/", &save_ptr);
  if((thread_current() -> dir == NULL) ||  // on start up
      (len != 0 && _name[0] == '/')) // "/xxxx/yyy"
  {
    // printf("absolute: %s\n", _name);
    cur_dir = dir_open_root();
  }
  else
    cur_dir = dir_reopen(thread_current() -> dir);
  // if(len != 0)
  // printf("survived initial open.\n");
  inode_down_reader_sema(cur_dir -> inode);
  for (; dir_name != NULL;
       dir_name = strtok_r (NULL, "/", &save_ptr))
  {
    // printf("cur inode num:%d\n", inode_get_inumber(dir_get_inode(cur_dir)));
    next_dir = NULL;
    next_inode = NULL;
    if (!strcmp(dir_name, ".")) // "./" case
      continue;
    else if (!strcmp("..", dir_name)) // "../" case
    {
      next_dir = dir_open_parent(dir_get_inode(cur_dir));
      inode_up_reader_sema(cur_dir -> inode);
      dir_close(cur_dir);
      cur_dir = next_dir;
      inode_down_reader_sema(cur_dir -> inode);
      prev_dir = dir_open_parent(dir_get_inode(cur_dir));
      inode_down_reader_sema(prev_dir -> inode);
    }
    else
    {
      if (!dir_lookup(cur_dir, dir_name, &next_inode) || !inode_is_dir(next_inode))
      {
        // printf("HERE! %s %d %d", dir_name, !dir_lookup(cur_dir, dir_name, &next_inode), cur_dir == NULL);
        // char str[NAME_MAX + 1];
        // dir_readdir(cur_dir, str);
        // printf(" %s\n", str);
        inode_up_reader_sema(cur_dir -> inode);
        dir_close(cur_dir);
        if(prev_dir != NULL)
        {
          inode_up_reader_sema(prev_dir -> inode);
          dir_close(prev_dir);
        }
        if (next_inode != NULL)
          inode_close(next_inode);
        free(name);
        return NULL;
      }
      else
      {
        // printf("is root: %d\n", inode_get_inumber(dir_get_inode(cur_dir)) == ROOT_DIR_SECTOR);
        inode_down_reader_sema(next_inode);
        next_dir = dir_open(next_inode);
        if(prev_dir != NULL)
        {
          inode_up_reader_sema(prev_dir -> inode);
          dir_close(prev_dir);
        }
        prev_dir = cur_dir;
        cur_dir = next_dir;
      }
    }
  }
  if(prev_dir != NULL)
  {
    inode_up_reader_sema(prev_dir -> inode);
    dir_close(prev_dir);
  }
  free(name);
  inode_up_reader_sema(cur_dir -> inode);
  return cur_dir;
}


/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry), true);
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  // printf("IS ROOT?: %d\n", inode_is_dir(inode));
  ASSERT (inode_is_dir(inode));
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      // printf("CLOSING DIR\n");
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);
  // printf("INODE LENGTH: %d\n", inode_length(dir->inode));
  inode_length(dir->inode);
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
  {
    /*printf("LOOKUP: |%s|%s|, sector: %d, in_use%d\n", name, e.name, e.inode_sector,e.in_use);*/
    if (e.in_use && !strcmp (name, e.name)) 
    {
      if (ep != NULL)
        *ep = e;
      if (ofsp != NULL)
        *ofsp = ofs;
      // printf("FOUND.\n");
      return true;
    }
  }
  // printf("NOT FOUND.\n");
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);
  // printf("STUCK?\n");
  inode_down_reader_sema(dir_get_inode(dir));
  // printf("NO.\n");
  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;
  inode_up_reader_sema(dir_get_inode(dir));
  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);
  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX || *(name + strlen (name) - 1) == '/')
    return false;
// printf("1\n");
  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;
// printf("2\n");
  /* Set the parent of the dir. */
  if (!inode_set_parent(inode_sector, inode_get_inumber(dir->inode)))
    goto done;
// printf("3\n");
  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  // printf("NAME: %s\n", e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
// printf("ADD DIR DONE.\n");
 done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);
  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  // printf("open cnt of a: %d\n", inode_get_open_cnt(inode));
  /* The directory is empty and not in use. */
  if (inode_is_dir(inode) && (!dir_is_empty(inode) || inode_get_open_cnt(inode) > 1))
    goto done;
  // printf("2\n");
  inode_down_all_sema(dir_get_inode(dir));
  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;
  inode_up_all_sema(dir_get_inode(dir));
 done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;
  inode_down_reader_sema(dir_get_inode(dir));
  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          inode_up_reader_sema(dir_get_inode(dir));
          return true;
        } 
    }
  inode_up_reader_sema(dir_get_inode(dir));
  return false;
}

/*
  Opens the parent directory of the INODE 
  and returns a directory for it.
*/
struct dir *
dir_open_parent (struct inode *inode)
{
  inode_down_reader_sema(inode);
  struct dir* dir = dir_open (inode_open (inode_get_parent(inode)));
  inode_up_reader_sema(inode);
  return dir;
}

/*
  Determine whether the directory with inode INODE
  is empty or not.
*/
bool
dir_is_empty(struct inode *inode)
{
  struct dir_entry de;
  off_t offset = 0;
  inode_down_reader_sema(inode);
  while (inode_read_at (inode, &de, sizeof de, offset) == sizeof de) 
  {
    offset += sizeof de;
    if (de.in_use)
    {
      inode_up_reader_sema(inode);
      return false;
    }
  }
  inode_up_reader_sema(inode);
  return true;
}
/*
  Parse the whole file NAME to the path to it and its file name.
  returns NULL if the name is invalid (i.e. ends with ".", "..", or "/", or an empty string.)
*/
char** parse_to_path_and_file_name(const char* name)
{
  char **result;
  size_t len = strlen(name);
  if(len < 1 || (len > 1 && name[len - 1] == '/'))
    return NULL;
  // printf("survived initial judge.\n");
  result = calloc(2, sizeof(char*));
  char *path_name=NULL, *file_name=NULL;
  size_t pos = len;
  while(pos--)
  {
    if(name[pos] == '/' || pos == 0)
    {
      // printf("IN\n");
      path_name = calloc(pos + 2, sizeof(char));
      file_name = calloc(NAME_MAX + 1, sizeof(char));
      if(pos == 0 && name[pos] != '/')
      {
        strlcpy(path_name, name, 1);
        strlcpy(file_name, name, NAME_MAX + 1);
      }
      else
      {
        // printf("YES!\n");
        if(pos == 0)
          strlcpy(path_name, name, pos + 2);
        else
          strlcpy(path_name, name, pos + 1);
        // printf("%d %d %d\n", name[pos] == '/', pos == 0, strcmp(path_name, "/"));
        strlcpy(file_name, name + pos + 1, NAME_MAX + 1);
        // printf("%d %d %d\n", name[pos] == '/', pos == 0, strcmp(file_name, "a"));
      }
      // printf("OK on separation: path:%s file:%s\n", path_name, file_name);
      result[0] = path_name;
      result[1] = file_name;
      return result;
    }
  }
  free(result);
  return NULL;
}

void
dir_output(struct inode *inode)
{
  size_t ofs;
  struct dir_entry e;
  printf("==========\n");
  for (ofs = 0; inode_read_at (inode, &e, sizeof e, ofs) == sizeof e;
    ofs += sizeof e) 
{printf("INODE %d: name: %s sector: %d, in_use%d\n", inode_get_inumber(inode), e.name, e.inode_sector,e.in_use);    }
  printf("==========\n");
}

/* Opens and returns the directory for the given FILE.
  Returns a null pointer on failure. */
struct dir *
dir_open_from_file (struct file *file) 
{
  // printf("IS ROOT?: %d\n", inode_is_dir(inode));
  struct inode *inode = file_get_inode(file);
  ASSERT (inode_is_dir(inode));
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = file_tell(file);
      return dir;
    }
  else
    {
      free (dir);
      return NULL; 
    }
}

/* Closes the DIR and stores its offset to the given FILE.
  Returns a null pointer on failure. */
void
dir_close_to_file (struct dir* dir, struct file *file) 
{
  // printf("IS ROOT?: %d\n", inode_is_dir(inode));
  struct inode *inode = dir_get_inode(dir);
  ASSERT (inode_is_dir(inode));
  file_seek (file, dir->pos);
  free(dir);
}