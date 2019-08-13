#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"

struct bitmap;

void inode_init (void);

//bool inode_direct_append(struct inode_disk*, size_t num_sectors_for_direct);
//bool inode_singly_indirect_append(struct inode_disk*, size_t num_sectors_for_indirect);
//bool inode_doubly_indirect_append(struct inode_disk*, size_t num_sectors_for_doubly_indirect);

bool inode_create (block_sector_t, off_t, int32_t);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);

//void release_all_entries(struct inode_disk*);

void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

void inode_cache_init(void);
void cache_read_at(block_sector_t sector, void *buffer);
void cache_write_at(block_sector_t sector, void *buffer);
void cache_flush(void);


void set_root_is_directory(void);
bool is_dir(struct inode *);
int inode_open_cnt(struct inode *);
bool inode_is_root(struct inode *);

bool most_recent_cache_search(void);

#endif /* filesys/inode.h */
