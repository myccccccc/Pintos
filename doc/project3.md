Design Document for Project 3: File System
==========================================

## Group Members

* Jiasheng Qin <jqin0713@berkeley.edu>
* Jingqi Wang <jackiewang@berkeley.edu>
* Yingchun Ma <mayingchun@berkeley.edu>
* Zewei Ding <ding.zew@berkeley.edu>

--------------------------

**Task 1**

**Data structures and functions**

**In `inode.c`:**
```C

#define CACHE_BLOCKS_NUM 64;

struct cache_block cache_blocks[CACHE_BLOCKS_NUM]; /* Array of cache_block */                         
unsigned clock_index;                           /* Current position of the clock hand for clock algorithm */

struct cache_block {
    block_sector_t sector_idx;    /* The sector on disk that this cache_block is uesed for */
    void *data;               /* Raw data*/
    struct lock block_lock;   
    bool valid;               /* True if this cache_block is caching a sector */
    bool dirty;               /* Dirty bit */
    bool recently_used;       /* Flag for clock algorithm (evict if false) */
}

void filesys_cache_init();

void cache_read_at(block_sector_t sector, void void *buffer, off_t size, off_t block_ofs);

void cache_write_at(block_sector_t sector, const void *buffer, off_t size, off_t block_ofs);

void cache_flush();
```

**Algorithms**

In `filesys_cache_init()`, we initialize `clock_index`, `cache_blocks`.  


In `cache_read_at(...)` and `cache_write_at(...)`, we need to iterate through the entries to check if the `sector` exists in the cache.  Then, for `cache_read_at(...)`, if the `sector` exists, we just read it into the `buffer`. If not we run the clock algorithm on `cache_blocks` to evict a entry and load the new entry into its place.  If we are doing `cache_write_at(...)`, if the `sector` is loaded into the cache, we directly write to it and set the `dirty` bit to true.  Otherwise, we find an entry to evict again, write it out to disk, then load the `sector` we are writing to into the cache and write to it.  Also, whenever a `cache_block` is read or written to, we set the `recently_used` flag to `true`.

In `cache_flush()`, we iterate through `cache_blocks` and directly write all dirty entries into the appropriate `block_sector_t` using the `block_write(...)` function.

**Synchronization**

For concurrent reads or writes to the same sector there will have to be usage of the `b

**Rationale**

This implementation is simple. We limit the cache to 64 sectors, using the clock algorithm,, we implement a write-back cache that flushes on eviction/shutdown, we don't use a bounce buffer.

The only issues is that finding a sector in the cache takes time linear to the number of the cache size because we need to iterate the array to find the corresponding `block_sector_t`. 

--------------------------

**Task 2**

**Data structures and functions:**

First, the `inode` struct will be altered. Currently, one `block_sector_t` is supported inside each `inode`. Similar to discussion 11, we will change this into 12 direct pointers:

`block_sector_t direct[12];`

1 indirect pointer:

`block_sector_t indirect;`

And 1 doubly indirect pointer:

`block_sector_t doubly indirect;`

One last addition, for synchronization purposes, will be a list of locks:

`struct list lock_list;`

Each lock will correspond to a certain disk sector (determined by its index in the list) that some part of the file corresponding to the `inode` occupies, as discussed in the synchronization section. The list will be initialized in `inode_init`.

**Algorithms:**

Since the maximum size that must be supported is 2^23 bytes, while our blocks are each 512 bytes (2^9) bytes in size, we must support a total of 2^14 blocks. One double indirect pointer supports 512/4 * 512/4 = 2^14 blocks by itself already (calculated based on the fact that each `block_sector_t`, denoting an address to be used during the indirection process, is a `uint32` type, which is 4 bytes long, and noting that doubly indirect pointers branch out twice, hence the squaring), but we include direct pointers and a (singly) indirect pointer in order to speed up file access.

A block pointed to by an indirect pointer will only contain `block_sector_t` values and NO data; as for a doubly indirect pointer, this principle applies twice. That is, the “doubly indirect block” a double indirect pointer points to AND the second layer of 512/4 indirect blocks that the doubly indirect block redirects to will only contain `block_sector_t` values.

`inode_write_at` will be changed as follows: if a write can be wholly contained by direct pointers (that is, the write would END at less than or equal to byte 12 * 512 - 1), the `direct` array will be filled in according to which blocks are written to (for example, if we wrote from byte 0 a total of 1000 bytes, then `direct[0]` and `direct[1]` would be filled in). New blocks pointed to by non-zero entries of `direct` will be allocated via `block_register` (inside block.c), so in the example from before, 2 new blocks are created.

 If a write can no longer be contained by the direct pointers (but still ENDS at less than or equal to byte 12 * 512 + 512 / 4 * 512 - 1), we must allocate an “indirect block” corresponding to `indirect`. At this point, `indirect` will be set to be the address of the “indirect” block. The write first appends to the end of the indirect block, and then writes to the appropriate block. For example, consider the case where we want to write to byte 12 * 512 + 1050. The appropriate block here is the one pointed to by the third `block_sector_t` (which corresponds to bytes 12 * 512 + 2 * 512 = 12 * 512 + 1024 through 12 * 512 * 3* 512 - 1 = 12 * 512 + 1535) inside the indirect block. If the `block_sector_t` that we want to write to is zeroed, we fill it in and allocate a data block to begin writing to (which the new `block_sector_t` now points to).

Finally, if a write corresponds to parts of the file that cannot be contained even by the indirect pointer, we must use the doubly indirect pointer. First, we allocate a “doubly indirect block”. As we fill this block in with `block_sector_t` entries, we create corresponding indirect blocks, as well as data blocks corresponding to these indirect blocks. The math for which block to write to is similar to that of the indirect pointer, except we start counting from byte 12 * 512 + 512 / 4 * 512.

NOTE: if any write extends beyond the current EOF (specified by the inode’s `length` parameter), we would need to increase the value of `length` appropriately. In addition, if we start a write past EOF, data blocks (or portions of data blocks) between the EOF and the start position will be zeroed out (we use the word “between” here in the sense that two data blocks are adjacent if they correspond to adjacent portions of a file). Our current plan is to use the first implementation discussed in section 3.4.2 inside the project instructions: “Some file systems allocate and write real data blocks for these implicitly zeroed blocks.”

Notice that once a `direct` entry, `indirect`, or `doubly_indirect` are defined, as well as any `block_sector_t` entries inside the (singly or doubly) indirect blocks, they cannot be changed except to be zeroed out through file deletion (i.e.` filesys_remove`). When this occurs, we must (potentially) recursively traverse the pointers present inside the `inode` struct in order to free not only data blocks but also (singly and doubly) indirect blocks. We plan to do this in a reverse breadth-first order (e.g. data blocks first, then indirect blocks, then the doubly indirect block when dealing with `doubly_indirect`). Afterwards, we will zero out `direct` entries and also `indirect` and doubly_indirect`.

Reading blocks is relatively similar to the file deletion process, except instead of deleting as we traverse this tree-like structure, we simply read into the buffer. That is, `inode_read_at` will first read from the blocks pointed to by `direct` entries, then blocks pointed by the indirect block (itself pointed to by `indirect`), and finally blocks pointed to by the indirect blocks pointed to by the  doubly indirect block (itself pointed to by `doubly_indirect`).

As for the implementation of the `inumber` syscall, we will return `direct[0]`. This is guaranteed to be unique for a given file descriptor, since `block_register` always allocates free blocks and thus does not cause any overlap.

On one last note, regarding memory exhaustion: if `block_register` is unable to allocate a new block using malloc, it currently panics. This can simply be changed to return NULL, terminating the method prematurely. At that point, when we try to expand a file and see that `block_register` has returned NULL, we can just abort the expansion and notify the user somehow (perhaps via a printf) that there is no more disk space. In order to not enter an unsafe state, we can perform all necessary `block_register` calls at the beginning of `inode_write_at`, so that if any of them fail, we can promptly return 0 and be ensured that no data was modified. If there were some blocks that were **successfully** allocated, we can just free them before returning.

**Synchronization:**	

Upon creation of a new data block (equal in space to a disk sector) for a given file (and thus its corresponding `inode`), we will add a new lock to `lock_list` inside that `inode`. Consider a write to bytes 100 to 200 for a particular file. All of this is contained in the block pointed to by the corresponding inode’s `direct[0]`; this block can be thought of as having a “block id” of 0. Thus, our write here will be gated off by `lock_acquire(<this_inode.lock_list’s 0th entry>)` and its corresponding `lock_release`. All entries of `direct` will have an equivalent index to their block id. That is, `direct[j]` will have block id j for all 0 <= j < 12. This will then correspond to the jth lock in `lock_list`.

Data blocks that are pointed to by indirect blocks will need to undergo a slight mathematical manipulation when determining their block id; the 0th `block_sector_t` entry inside the indirect block corresponding to `indirect` will have block id 12; for all 128 (i.e. 512/4) entries inside the indirect block, the offset of 12 will be added.  When we get to the indirect block, we start our block ids at 12 + 128 = 140. We will add 128 * <indirect block index> + <entry number inside indirect block> to get the final block id. For example, consider the third `block_sector_t` entry inside the doubly indirect block (pointed to by `doubly indirect`). Suppose that we want to write to the fourth entry listed inside the indirect block that is pointed to by the entry mentioned in the last sentence. In that case, we would be looking at block id 140 + 128 * 3 + 4 = 528.

There are two important things to note here. The first is that, for a given `inode`, if its associated file occupies a block with id X, then it MUST also occupy all blocks with ids 0 through X – 1. This is due to the nature of our file methods: if we issue a write that starts past the EOF, this allocates any necessary blocks for the zeroed values between the EOF and the starting point, thus ensuring continuity. In addition, we are not required to support removing certain parts of a file; the only case where a file has data deleted is when the entire file is removed, in which case the `lock_list` would be emptied out. The contiguity property ensures that we can use locks simply based on their indices in `lock_list`; if the Xth entry inside `lock_list` exists, then it must correspond to the block with id equal to X.

The second thing to note is that, in order to ensure a sort of atomicity in writes (that is, one write should not affect another write before it is fully finished), **all** necessary locks will be acquired and released surrounding the entirety of the writing process. That is, if we want to write to blocks with ids X through Y, we must [consecutively] perform `lock_acquire(<this_inode.lock_list’s Xth entry>);`, `lock_acquire(<this_inode.lock_list’s [X+1]th entry>);`, …, `lock_acquire(<this_inode.lock_list’s Yth entry>);`, and the corresponding releases at the end.

**Rationale:**

The idea of having 12 direct pointers, 1 indirect pointer, and 1 double indirect pointer is brought from both lecture and discussion. As for the practical justification, having some direct pointers ensures that smaller files (which are the majority of user files in practice) can be accessed quickly, while using (single and doubly) indirect pointers also supports larger files without occupying too much space (inside each `inode` struct). 

As for the `lock_list`, other ideas we thought of were using an array that was pre-sized to be the maximum number of blocks possible for any given file (which we rejected due to the immensely unnecessary memory consumption, although arrays are certainly faster in terms of access time – getting a lock is linear with `lock_list` in contrast to constant time with an array) and using a global variable `lock_to_block_id_list` which contained `lock_to_block_id` structs (which we rejected since it was more complicated than it needed to be; by using the contiguity principle, we could instead just use `lock_list` based on its indices instead of needing some extra variable to keep track of block id).

We plan to potentially have some helper methods to calculate block id based on byte number in a given file, which will help to simplify the code, but overall, implementation of task 2 does not require too many drastic changes (only the additions to the `inode` struct and more complex read and write methods), and should not be too difficult to code.

--------------------------

**Task 3**

**Data Structures and Functions:**

In thread.c:

```c
struct thread
{
	...
	struct dir  *cur_dir; 	
	/* The thread's current working directory /
	...
}
  
```
In inode.c :
```c
struct inode
{
	...
	bool is_dir;  
	/* indicate if inode is a directory or not */
	...
}

```

**Algorithms:**

We add a `struct dir  *cur_dir` in `struct thread` to keep track of the thread's current working directory.
In order to support for relative paths for any syscall, we will use the current directory as the start of our search.

We also add a `bool is_dir` in `struct inode` to indicate whether the inode is corresponding to a directory or a file.

Some baisc implementations for syscalls:

`bool chdir (const char *dir)`:
 Can simply set the `cur_dir` to the directory passed in.

`bool mkdir (const char *dir)`: 
Create a new file with the `is_dir` set to `true`.

`bool readdir (int fd, char *name)`: 
By calling the `dir_readdir()`  in `directory.c`.

`bool isdir (int fd)`: 
Map the file descriptor to a inode and return the `is_dir` for the corresponding inode.

`int inumber (int fd)`: 
Map the file descriptor to a inode and call `inode_get_inumber()` in `inode.c`.



**Synchronization:**

The synchronization problem should been handled in the buffer cache. Since we have already add a lock for each sector, there is no need to consider synchronization when adding subdirectories to the filesystem.

**Rationale:**

Adding a pointer to the current working directory for each thread is the most straightforward way.  And adding a `bool is_dir`for each inode is also the most intuitive way to distinguish between a directory and a file.
 


--------------------------

**Additional Question**

_Write-behind_: This can be implemented inside `thread_tick` in thread.c, such that every X ticks (determined by the regularity of the flushing), we will look through the buffer cache and see if any blocks have their dirty bit set. If so, we will write to disk, as described in our Task 1 description.

_Read-ahead_: We can also place our read-ahead in the same place as our write-behind implementation; however, read-ahead is more complex. Namely, we need to follow two principles when predicting what files a user will access next: spatial and temporal locality. In the former case, files that are “adjacent” (for example, files in the same directory) are likely to be sequentially accessed, while the latter specifies that if a file is accessed once, it is likely to be accessed again the near future. For spatial locality, if the cache has space, we may choose to preemptively bring in adjacent files (e.g. by reading in certain files from the same directory). For temporal locality, we could choose to add another parameter for each cache entry – one that keeps track of how many times a file is accessed while it’s in the cache. By default, the cache is a FIFO queue; however, by using this extra parameter, we can set up a sort of priority system, such that we evict the first file to come in **with the lowest number of accesses**.
