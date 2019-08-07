#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"

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

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry), 1);
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode)
{
  if (is_dir(inode) == false)
  {
    inode_close (inode);
    return NULL;
  }
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

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    if (e.in_use && !strcmp (name, e.name))
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode)
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

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
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

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
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

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
    return success;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    return success;

  if (!is_dir(inode))
  {
    /* Erase directory entry. */
    e.in_use = false;
    if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e)
    {
      inode_close (inode);
      return success;
    }

    /* Remove inode. */
    inode_remove (inode);
    success = true;

    inode_close (inode);
    return success;
  }
  else
  {
    struct dir *inode_dir = dir_open(inode);
    if (inode_open_cnt(inode) == 1 && dir_isempty(inode_dir))
    {
      /* Erase directory entry. */
      e.in_use = false;
      if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e)
      {
        dir_close(inode_dir);
        return success;
      }

      /* Remove inode. */
      inode_remove (inode);
      success = true;

      dir_close(inode_dir);
      return success;
    }
    else
    {
      dir_close(inode_dir);
      return success;
    } 
  }
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e)
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        }
    }
  return false;
}

/* Extracts a file name part from *SRCP into PART, and updates *SRCP so that the
next call will return the next file name part. Returns 1 if successful, -1 part is set to the last name in the string, 0 at
end of string part is not set, -2 for a too-long file name part. */
int get_next_part (char part[NAME_MAX + 1], char **srcp) 
{
  char *src = *srcp;
  char *src2;
  char *dst = part;
  /* Skip leading slashes. If it’s all slashes, we’re done. */
  while (*src == '/')
    src++;
  if (*src == '\0')
    return 0;
  /* Copy up to NAME_MAX character from SRC to DST. Add null terminator. */
  while (*src != '/' && *src != '\0') {
    if (dst < part + NAME_MAX)
      *dst++ = *src;
    else
      return -2;
    src++;
  }
  *dst = '\0';
  /* Advance source pointer. */
  *srcp = src;

  src2 = src;
  while (*src2 == '/')
    src2++;
  if (*src2 == '\0')
    return -1;
  return 1;
}

struct dir *get_dir(char *name, char *file_name)
{
  struct dir *start_dir = NULL;
  int end_of_dir_name;
  struct inode *file_inode;
  if (strcmp(name, "") == 0)
  {
    return NULL;
  }
  if (name[0] == '/')
  {
    start_dir = dir_open_root();
  }
  else
  {
    start_dir = dir_reopen(thread_current()->cwd);
  }
  while (true)
  {
    if (start_dir == NULL)
    {
      return false;
    }
    end_of_dir_name = get_next_part(file_name, &name);
    if (end_of_dir_name == -2 || end_of_dir_name == 0)
    {
      dir_close(start_dir);
      return NULL;
    }
    else if (end_of_dir_name == 1)
    {
      if (dir_lookup(start_dir, file_name, &file_inode) == false)
      {
        dir_close(start_dir);
        return NULL;
      }
      dir_close(start_dir);
      start_dir = dir_open(file_inode);
    }
    else
    {
      return start_dir;
    }
  }
}

struct dir *goto_dir(char *name)
{
  if (strcmp(name, "") == 0)
  {
    return NULL;
  }
  char name_add_dot[strlen(name) + 2 + 1];
  strlcpy(name_add_dot, name, strlen(name) + 1);
  name_add_dot[strlen(name)] = '/';
  name_add_dot[strlen(name)+1] = '.';
  name_add_dot[strlen(name)+2] = '\0';
  char file_name[NAME_MAX+1];
  struct inode *file_inode;
  struct dir *start_dir = get_dir(name_add_dot, file_name);
  if (start_dir == NULL)
  {
    return NULL;
  }
  if (dir_lookup(start_dir, file_name, &file_inode) == false)
  {
    dir_close(start_dir);
    return NULL;
  }
  dir_close(start_dir);
  start_dir = dir_open(file_inode);
  return start_dir;
}

void add_parent(struct dir *dir, struct dir *parent_dir)
{
  dir_add(dir, ".", inode_get_inumber(dir->inode));
  dir_add(dir, "..", inode_get_inumber(parent_dir->inode));
}

void dir_seek(struct dir *dir, off_t pos)
{
  dir->pos = pos;
}

off_t dir_tell (struct dir *dir)
{
  return dir->pos;
}

bool dir_isempty (struct dir *dir)
{
  char name[NAME_MAX+1];
  while(dir_readdir(dir, name))
  {
    if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0)
    {
      return false;
    }
  }
  return true;
}