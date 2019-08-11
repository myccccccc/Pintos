#ifndef FILESYS_DIRECTORY_H
#define FILESYS_DIRECTORY_H

#include <stdbool.h>
#include <stddef.h>
#include "devices/block.h"
#include "off_t.h"

/* Maximum length of a file name component.
   This is the traditional UNIX maximum length.
   After directories are implemented, this maximum length may be
   retained, but much longer full path names must be allowed. */
#define NAME_MAX 14
#define DEFAULT_ENTRY_NUM 128

struct inode;

/* Opening and closing directories. */
bool dir_create (block_sector_t sector, size_t entry_cnt);
struct dir *dir_open (struct inode *);
struct dir *dir_open_root (void);
struct dir *dir_reopen (struct dir *);
void dir_close (struct dir *);
struct inode *dir_get_inode (struct dir *);

/* Reading and writing. */
bool dir_lookup (const struct dir *, const char *name, struct inode **);
bool dir_add (struct dir *, const char *name, block_sector_t);
bool dir_remove (struct dir *, const char *name);
bool dir_readdir (struct dir *, char name[NAME_MAX + 1]);

struct dir *get_dir(char *name, char *file_name);
struct dir *goto_dir(char *name);
void add_parent(struct dir *dir, struct dir *parent_dir);
int get_next_part (char part[NAME_MAX + 1], char **srcp);
void dir_seek (struct dir *dir, off_t pos);
off_t dir_tell (struct dir *dir);
bool dir_isempty (struct dir *);
#endif /* filesys/directory.h */
