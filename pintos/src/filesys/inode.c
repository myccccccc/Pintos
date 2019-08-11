#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"

struct cache_block {
    block_sector_t sector_idx;    /* The sector on disk that this cache_block is uesed for */
    void *data;                   /* Raw data*/
    struct lock block_lock;       /* There can only be one thread in this cache_block */
    bool dirty;                   /* Dirty bit */
    int recently_used;            /* Flag for clock algorithm */
    bool valid;                   /* True if this cache_block is caching a sector */
};

/* Total number of cache blocks */
#define CACHE_BLOCKS_NUM 64

/* Array of cache_block */
struct cache_block cache_blocks[CACHE_BLOCKS_NUM];

/* Current position of the clock hand for clock algorithm */
unsigned clock_index;

/* There can only be one thread in cache_blocks */
struct lock cache_blocks_lock;

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    int32_t is_directory;               /* 1 is a directory, -1 is not a directory */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[124];               /* Not used. */
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
    struct inode_disk data;             /* Inode content. */
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, int32_t is_directory)
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
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->is_directory = is_directory;
      disk_inode->magic = INODE_MAGIC;
      if (free_map_allocate (sectors, &disk_inode->start))
        {
          //block_write (fs_device, sector, disk_inode);
          cache_write_at (sector, disk_inode);
          if (sectors > 0)
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;

              for (i = 0; i < sectors; i++)
                //block_write (fs_device, disk_inode->start + i, zeros);
                cache_write_at (disk_inode->start + i, zeros);
            }
          success = true;
        }
      free (disk_inode);
    }
  return success;
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
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  //block_read (fs_device, inode->sector, &inode->data);
  cache_read_at (inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
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

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          free_map_release (inode->sector, 1);
          free_map_release (inode->data.start,
                            bytes_to_sectors (inode->data.length));
        }

      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
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
          //block_read (fs_device, sector_idx, buffer + bytes_read);
          cache_read_at (sector_idx, buffer + bytes_read);
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
          //block_read (fs_device, sector_idx, bounce);
          cache_read_at (sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

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

  if (inode->deny_write_cnt)
    return 0;

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
          //block_write (fs_device, sector_idx, buffer + bytes_written);
          cache_write_at (sector_idx, (void *) buffer + bytes_written);
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
            //block_read (fs_device, sector_idx, bounce);
            cache_read_at (sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          //block_write (fs_device, sector_idx, bounce);
          cache_write_at (sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

void inode_cache_init()
{
  int index;
  for (index = 0; index < CACHE_BLOCKS_NUM; index++)
  {
    lock_init(&cache_blocks[index].block_lock);
    cache_blocks[index].data = malloc(BLOCK_SECTOR_SIZE);
    cache_blocks[index].dirty = false;
    cache_blocks[index].recently_used = 0;
    cache_blocks[index].valid = false;
  }
  clock_index = 0;
  lock_init(&cache_blocks_lock);
}

void cache_read_at(block_sector_t sector, void *buffer)
{
  int index;
  bool find_a_cache_block;
  lock_acquire(&cache_blocks_lock);
  for (index = 0; index < CACHE_BLOCKS_NUM; index++)
  {
    if (cache_blocks[index].valid == true && cache_blocks[index].sector_idx == sector)
    {
      break;
    }
  }
  if (index != CACHE_BLOCKS_NUM)
  {
    lock_release(&cache_blocks_lock);
    lock_acquire(&cache_blocks[index].block_lock);
    if (cache_blocks[index].sector_idx != sector)
    {
      lock_release(&cache_blocks[index].block_lock);
      cache_read_at(sector, buffer);
    }
  }
  else
  {
    find_a_cache_block = false;
    index = clock_index;
    while (!find_a_cache_block)
    {
      for (index = index % CACHE_BLOCKS_NUM; index < CACHE_BLOCKS_NUM; index++)
      {
        if (lock_try_acquire(&cache_blocks[index].block_lock))
        {
          if (cache_blocks[index].valid == false || cache_blocks[index].recently_used == 0)
          {
            find_a_cache_block = true;
            break;
          }
          else
          {
            cache_blocks[index].recently_used = 0;
            lock_release(&cache_blocks[index].block_lock);
          }
        }
      }
    }
    clock_index = index;
    if (cache_blocks[index].dirty == true)
    {
      block_write (fs_device, cache_blocks[index].sector_idx, cache_blocks[index].data);
    }
    cache_blocks[index].sector_idx = sector;
    cache_blocks[index].valid = true;
    cache_blocks[index].dirty = false;
    cache_blocks[index].recently_used = 1;
    lock_release(&cache_blocks_lock);
    block_read(fs_device, cache_blocks[index].sector_idx, cache_blocks[index].data);
  }
  memcpy(buffer, cache_blocks[index].data, BLOCK_SECTOR_SIZE);
  lock_release(&cache_blocks[index].block_lock);
}

void cache_write_at(block_sector_t sector, void *buffer)
{
  int index;
  bool find_a_cache_block;
  lock_acquire(&cache_blocks_lock);
  for (index = 0; index < CACHE_BLOCKS_NUM; index++)
  {
    if (cache_blocks[index].valid == true && cache_blocks[index].sector_idx == sector)
    {
      break;
    }
  }
  if (index != CACHE_BLOCKS_NUM)
  {
    lock_release(&cache_blocks_lock);
    lock_acquire(&cache_blocks[index].block_lock);
    if (cache_blocks[index].sector_idx != sector)
    {
      lock_release(&cache_blocks[index].block_lock);
      cache_write_at(sector, buffer);
    }
  }
  else
  {
    find_a_cache_block = false;
    index = clock_index;
    while (!find_a_cache_block)
    {
      for (index = index % CACHE_BLOCKS_NUM; index < CACHE_BLOCKS_NUM; index++)
      {
        if (lock_try_acquire(&cache_blocks[index].block_lock))
        {
          if (cache_blocks[index].valid == false || cache_blocks[index].recently_used == 0)
          {
            find_a_cache_block = true;
            break;
          }
          else
          {
            cache_blocks[index].recently_used = 0;
            lock_release(&cache_blocks[index].block_lock);
          }
        }
      }
    }
    clock_index = index;
    if (cache_blocks[index].dirty == true)
    {
      block_write (fs_device, cache_blocks[index].sector_idx, cache_blocks[index].data);
    }
    cache_blocks[index].sector_idx = sector;
    cache_blocks[index].valid = true;
    cache_blocks[index].recently_used = 1;
    cache_blocks[index].dirty = true;
    lock_release(&cache_blocks_lock);
    block_read(fs_device, cache_blocks[index].sector_idx, cache_blocks[index].data);
  }
  memcpy(cache_blocks[index].data, buffer, BLOCK_SECTOR_SIZE);
  cache_blocks[index].dirty = true;
  lock_release(&cache_blocks[index].block_lock);
}

void cache_flush(void)
{
  int index;
  for (index = 0; index < CACHE_BLOCKS_NUM; index++)
  {
    if (cache_blocks[index].valid && cache_blocks[index].dirty)
    {
      block_write(fs_device, cache_blocks[index].sector_idx, cache_blocks[index].data);
    }
    free(cache_blocks[index].data);
  } 
}

void set_root_is_directory(void)
{
  struct inode_disk root_inode_disk;
  cache_read_at(ROOT_DIR_SECTOR, &root_inode_disk);
  root_inode_disk.is_directory = 1;
  cache_write_at(ROOT_DIR_SECTOR, &root_inode_disk);
}

bool is_dir(struct inode *i)
{
  if (i->data.is_directory == 1)
  {
    return true;
  }
  return false;
}

int inode_innumber(struct inode *i)
{
  return i->sector;
}

int inode_open_cnt(struct inode *i)
{
  return i->open_cnt;
}