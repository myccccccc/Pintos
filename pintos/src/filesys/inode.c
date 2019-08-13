#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "devices/block.h"
#include "threads/synch.h"
#include <stdio.h>
#include <stdlib.h>

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Number of direct pointers per inode_disk*/
#define NUM_DIRECT 12

/* Total number of cache blocks */
#define CACHE_BLOCKS_NUM 100


struct cache_block {
    block_sector_t sector_idx; /* Sector on disk that this cache_block is used for*/
    void* data;                /* Raw data*/
    struct lock block_lock;    /* There can only be one thread in this cache_block */
    bool dirty;                /* Dirty bit */
    int recently_used;         /* Flag for clock algorithm */
    bool valid;                /* True if this cache_block is caching a sector */
};

/* Array of cache_block */
struct cache_block cache_blocks[CACHE_BLOCKS_NUM];

/* Current position of the clock hand for clock algorithm */
unsigned clock_index;

/* There can only be one thread in cache_blocks */
struct lock cache_blocks_lock;


/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk {
    //block_sector_t start;               /* First data sector. */

    block_sector_t direct[NUM_DIRECT]; /* Direct data pointers */
    block_sector_t indirect; /* Singly indirect pointer */
    block_sector_t doubly_indirect; /* Doubly indirect pointer */

    off_t length; /* File size in bytes. */
    unsigned magic; /* Magic number. */
    
    int32_t is_directory; /*1 is a directory, -1 is not a directory*/
    
    uint32_t unused[(BLOCK_SECTOR_SIZE - NUM_DIRECT * 4 - 20) / 4]; /* Not used. NUM_DIRECT*4 is the space in bytes for the direct pointers;
                                                                     20 is for the indirect/doubly indirect, the length, the magic, and is_dir */
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors(off_t size) {
    return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode {
    struct list_elem elem; /* Element in inode list. */
    block_sector_t sector; /* Sector number of disk location. */
    int open_cnt; /* Number of openers. */
    bool removed; /* True if deleted, false otherwise. */
    int deny_write_cnt; /* 0: writes ok, >0: deny writes. */
    struct inode_disk data; /* Inode content. */
    struct lock inode_lock; /* Lock for the inode */
};

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector(const struct inode *inode, off_t pos) {
    ASSERT(inode != NULL);
    if (pos < inode->data.length) {
        if (pos < NUM_DIRECT * BLOCK_SECTOR_SIZE) {
            //printf("Inspecting direct\n");
            return inode->data.direct[pos / BLOCK_SECTOR_SIZE];
        }
        else if (pos < NUM_DIRECT * BLOCK_SECTOR_SIZE + ENTRIES_PER_BLOCK * BLOCK_SECTOR_SIZE) {
            //printf("Inspecting indirect\n");
            int indirect_block_index = (pos - NUM_DIRECT * BLOCK_SECTOR_SIZE)/ BLOCK_SECTOR_SIZE;
            
            block_sector_t indirect_block[ENTRIES_PER_BLOCK];
            //block_read(fs_device, inode->data.indirect, indirect_block);
            cache_read_at(inode->data.indirect, indirect_block);
            
            return indirect_block[indirect_block_index];
        }
        else {
            //printf("Inspecting doubly indirect\n");
            int remaining_pos = pos - (NUM_DIRECT * BLOCK_SECTOR_SIZE + ENTRIES_PER_BLOCK * BLOCK_SECTOR_SIZE);
            //printf("Remaining pos is %d\n", remaining_pos);
            int doubly_indirect_block_index = remaining_pos / (ENTRIES_PER_BLOCK * BLOCK_SECTOR_SIZE);
            
            block_sector_t doubly_indirect_block[ENTRIES_PER_BLOCK];
            //block_read(fs_device, inode->data.doubly_indirect, doubly_indirect_block);
            cache_read_at(inode->data.doubly_indirect, doubly_indirect_block);
            
            block_sector_t singly_indirect_block_location = doubly_indirect_block[doubly_indirect_block_index];
            block_sector_t singly_indirect_block[ENTRIES_PER_BLOCK];
            //block_read(fs_device, singly_indirect_block_location, singly_indirect_block);
            cache_read_at(singly_indirect_block_location, singly_indirect_block);
            
            int singly_indirect_block_index = (remaining_pos % (ENTRIES_PER_BLOCK * BLOCK_SECTOR_SIZE)) / BLOCK_SECTOR_SIZE;
            return singly_indirect_block[singly_indirect_block_index];
        }
    }
    else {
        return -1;
    }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init(void) {
    list_init(&open_inodes);
}

//Returns ceil(x/y)
static int
ceil_int(int x, int y) {
    return x / y + (x % y == 0 ? 0 : 1);
}

// Appends direct data blocks to the specified INODE. The number appended must be at most NUM_DIRECT minus the current number of directs blocks.
// Does NOT modify the last DATA sector currently occupied (i.e. doesn't zero pad it).
static bool
inode_direct_append(struct inode_disk* disk_inode, size_t num_sectors_for_direct) {
    //Total number of DATA sectors across all pointers
    int total_current_sectors = ceil_int(disk_inode->length, BLOCK_SECTOR_SIZE);
    //Bound on upper end by NUM_DIRECT (lower bound is implicit due to being unable to have len < 0)
    int num_direct_sectors_occupied = total_current_sectors <= NUM_DIRECT ? total_current_sectors : NUM_DIRECT;
    //Cannot end up with a total of more than NUM_DIRECT direct pointers
    if ((int)num_sectors_for_direct > NUM_DIRECT - num_direct_sectors_occupied) {
        return false;
    }
    
    static char zeros [BLOCK_SECTOR_SIZE];

    bool direct_allocation_passed = false;
    int ind_direct;
    for (ind_direct = 0; ind_direct < (int)num_sectors_for_direct; ind_direct ++) {
        direct_allocation_passed = free_map_allocate(1, &(disk_inode->direct[num_direct_sectors_occupied + ind_direct]));
        if (!direct_allocation_passed) {
            break;
        }
    }
    //Rollback in case any memory allocations failed
    if (!direct_allocation_passed) {
        int temp;
        for (temp = 0; temp < ind_direct; temp ++) {
            free_map_release(disk_inode->direct[num_direct_sectors_occupied + temp], 1);
        }
    }
    else {
        //Now we actually fill in data, zeroed out; existing data is skipped over
        int ind_data;
        for (ind_data = 0; ind_data < (int)num_sectors_for_direct; ind_data ++) {
            //block_write(fs_device, disk_inode->direct[num_direct_sectors_occupied + ind_data], zeros);
            cache_write_at(disk_inode->direct[num_direct_sectors_occupied + ind_data], zeros);
        }
    }
    
    //Only update length if memory allocation was successful; will always end up as a multiple of BLOCK_SECTOR_SIZE, but inode_write_at overrides
    //this at the end with the appropriate value
    if (direct_allocation_passed) {
        disk_inode->length = (total_current_sectors + num_sectors_for_direct) * BLOCK_SECTOR_SIZE;
    }
    
    return direct_allocation_passed;
}

// Appends indirect data blocks to the specified INODE.
// Will create the singly indirect block if not already present.
// Does NOT modify the last DATA sector currently occupied (i.e. doesn't zero pad it).
// This method should only be called after the direct pointers are all completely in use, in order to maintain contiguity within the inode struct
static bool
inode_singly_indirect_append(struct inode_disk* disk_inode, size_t num_sectors_for_indirect) {
    int total_current_sectors = ceil_int(disk_inode->length, BLOCK_SECTOR_SIZE);
    
    //Checks to make sure that we have at least completely filled the direct pointers
    ASSERT(total_current_sectors >= NUM_DIRECT);
    
    //Bound on lower end by zero, upper end by ENTRIES_PER_BLOCK, i.e. the number of entries in the indirect block.
    int num_indirect_sectors_occupied = total_current_sectors - NUM_DIRECT >= 0 ? total_current_sectors - NUM_DIRECT : 0;
    num_indirect_sectors_occupied = num_indirect_sectors_occupied <= ENTRIES_PER_BLOCK ? num_indirect_sectors_occupied : ENTRIES_PER_BLOCK;
    //Cannot end up with more than ENTRIES_PER_BLOCK indirect sectors
    if ((int)num_sectors_for_indirect > ENTRIES_PER_BLOCK - num_indirect_sectors_occupied) {
        return false;
    }
    
    static char zeros [BLOCK_SECTOR_SIZE];
    
    //Return value
    bool indirect_allocation_passed = false;
    
    //Only need to allocate this special block if we have just exactly filled out the direct array
    bool allocate_singly_indirect_block = total_current_sectors == NUM_DIRECT;
    
    bool indirect_block_allocated = false;
    if (allocate_singly_indirect_block) {
        indirect_block_allocated = free_map_allocate(1, &disk_inode->indirect);
    }
    if (indirect_block_allocated || !allocate_singly_indirect_block) {
        //Now we need to allocate actual data blocks
        
        //Reads in entries inside the indirect block that are already present
        static block_sector_t singly_indirect_block_entries[ENTRIES_PER_BLOCK];
        //block_read(fs_device, disk_inode->indirect, singly_indirect_block_entries);
        cache_read_at(disk_inode->indirect, singly_indirect_block_entries);
        
        //Fill in appended entries at the end
        int ind_indirect;
        for (ind_indirect = 0; ind_indirect < (int)num_sectors_for_indirect; ind_indirect ++) {
            indirect_allocation_passed = free_map_allocate(1, &(singly_indirect_block_entries[num_indirect_sectors_occupied + ind_indirect]));
            if (!indirect_allocation_passed) {
                break;
            }
        }
        //Roll back in case any allocations failed
        if (!indirect_allocation_passed) {
            int temp;
            for (temp = 0; temp < ind_indirect; temp ++) {
                free_map_release(singly_indirect_block_entries[num_indirect_sectors_occupied + temp], 1);
            }
        }
        else {
            //block_write(fs_device, disk_inode->indirect, singly_indirect_block_entries);
            cache_write_at(disk_inode->indirect, singly_indirect_block_entries);

            //Notice that only appended data is filled out; we don't want to zero out existing data entries
            int ind_data;
            for (ind_data = 0; ind_data < (int)num_sectors_for_indirect; ind_data ++) {
                //block_write(fs_device, singly_indirect_block_entries[num_indirect_sectors_occupied + ind_data], zeros);
                cache_write_at(singly_indirect_block_entries[num_indirect_sectors_occupied + ind_data], zeros);
            }
        }
    }
    
    //Only update length if memory allocation was successful; will always end up as a multiple of BLOCK_SECTOR_SIZE, but inode_write_at overrides
    //this at the end with the appropriate value
    if (indirect_allocation_passed) {
        disk_inode->length = (total_current_sectors + num_sectors_for_indirect) * BLOCK_SECTOR_SIZE;
    }
    
    return indirect_allocation_passed;
}

// Appends doubly indirect data blocks to the specified INODE.
// Will create the doubly indirect block if not already present.
// Does NOT modify the last DATA sector currently occupied (i.e. doesn't zero pad it).
// This method should only be called after the direct pointers AND singly indirect pointers are all completely in use,
// in order to maintain contiguity within the inode struct
static bool
inode_doubly_indirect_append(struct inode_disk* disk_inode, size_t num_sectors_for_doubly_indirect) {
    int total_current_sectors = ceil_int(disk_inode->length, BLOCK_SECTOR_SIZE);
    
    //Check to make sure that we have filled in both direct pointers and (singly) indirect pointers
    ASSERT(total_current_sectors >= NUM_DIRECT + ENTRIES_PER_BLOCK);
    
    //Bound on lower end by zero, upper end bound due to following check
    int total_current_doubly_indirect_sectors = total_current_sectors - NUM_DIRECT - ENTRIES_PER_BLOCK >= 0 ?
        total_current_sectors - NUM_DIRECT - ENTRIES_PER_BLOCK : 0;
    //Restrict number of doubly indirect data sectors to add
    if ((int)num_sectors_for_doubly_indirect > ENTRIES_PER_BLOCK * ENTRIES_PER_BLOCK - total_current_doubly_indirect_sectors) {
        return false;
    }
    
    static char zeros [BLOCK_SECTOR_SIZE];
    
    //Return value
    bool double_indirect_allocation_passed = false;
    
    //Only need to allocate this special block if we have just exactly filled out the direct array and the indirect pointer space
    bool allocate_doubly_indirect_block = total_current_sectors == NUM_DIRECT + ENTRIES_PER_BLOCK;
    
    bool doubly_indirect_block_allocated = false;
    if (allocate_doubly_indirect_block) {
        //printf("Doubly indirect block allocated\n");
        doubly_indirect_block_allocated = free_map_allocate(1, &disk_inode->doubly_indirect);
    }
    if (doubly_indirect_block_allocated || !allocate_doubly_indirect_block) {
        static block_sector_t doubly_indirect_block_entries[ENTRIES_PER_BLOCK];
        //Read over current entries in doubly indirect block
        //block_read(fs_device, disk_inode->doubly_indirect, doubly_indirect_block_entries);
        cache_read_at(disk_inode->doubly_indirect, doubly_indirect_block_entries);
        
        //Now, check to make sure that the last occupied indirect block is completely filled out. If not, fill it out in order to conserve space.
        int effective_num_sectors = num_sectors_for_doubly_indirect;
        //printf("Must fill in %d sectors\n", effective_num_sectors);
        int last_sector_num_filled = total_current_doubly_indirect_sectors % ENTRIES_PER_BLOCK;
        if (last_sector_num_filled != 0) {
            //printf("Initial remainder filling\n");
            //Find the last indirect block that contains meaningful block_sector_t values
            block_sector_t last_occupied = doubly_indirect_block_entries[(total_current_doubly_indirect_sectors-1) / ENTRIES_PER_BLOCK];
            //Fill up to the very end of the last indirect block
            int num_to_fill = (int) num_sectors_for_doubly_indirect <= ENTRIES_PER_BLOCK - last_sector_num_filled ?
                (int) num_sectors_for_doubly_indirect : ENTRIES_PER_BLOCK - last_sector_num_filled;
            
            //Fill in existing entries
            static block_sector_t last_occupied_entries[ENTRIES_PER_BLOCK];
            //block_read(fs_device, last_occupied, last_occupied_entries);
            cache_read_at(last_occupied, last_occupied_entries);
            
            //Append at end
            int ind_last;
            for (ind_last = 0; ind_last < num_to_fill; ind_last ++) {
                double_indirect_allocation_passed = free_map_allocate(1, &(last_occupied_entries[last_sector_num_filled + ind_last]));
                if (!double_indirect_allocation_passed) {
                    break;
                }
            }
            //Roll back and return, no need to go through further allocations
            if (!double_indirect_allocation_passed) {
                int temp_free;
                for (temp_free = 0; temp_free < ind_last; temp_free++) {
                    free_map_release(last_occupied_entries[last_sector_num_filled + temp_free], 1);
                }
                return false;
            }
            else {
                //Fill in zeroed out data
                int ind_data;
                for (ind_data = 0; ind_data < num_to_fill; ind_data++) {
                    //block_write(fs_device, last_occupied_entries[last_sector_num_filled + ind_data], zeros);
                    cache_write_at(last_occupied_entries[last_sector_num_filled + ind_data], zeros);
                }
                //Write the filled out block back to memory
                cache_write_at(last_occupied, last_occupied_entries);

                //Change the number of sectors to be used in later calculations
                effective_num_sectors -= num_to_fill;
            }
        }
        //printf("Effective num sectors is %d\n", effective_num_sectors);
        
        //Consider the case where BLOCK_SECTOR_SIZE is 512. This allows us to store a total of 128 block_sector_t entries
        //inside an indirect block. If we need 129 sectors, then we would need ceil(129/128) = 2 indirect blocks to contain
        //that number of entries. Note that ceil(x/y) == x/y + (x%y == 0 ? 0 : 1).
        int num_whole_blocks_needed = effective_num_sectors / ENTRIES_PER_BLOCK;
        int num_remaining_sectors = effective_num_sectors % ENTRIES_PER_BLOCK;
        int total_indirect_blocks_needed = num_whole_blocks_needed + (num_remaining_sectors == 0 ? 0 : 1);

        //printf("Num whole blocks needed is %d, num remaining sectors is %d, total blocks needed is %d\n", num_whole_blocks_needed, num_remaining_sectors, total_indirect_blocks_needed);
        
        //Where to start indexing for the doubly indirect block.
        //Consider that there are 129 sectors currently occupied. This means indirect block 0 and 1 are used, i.e. up to 129/128 = 1.
        //We filled in indirect block 1 if there was any space remaining in the previous step. Thus, we must start at indirect block 2.
        //If we start at a multiple of 128 sectors, this is where the (initial) minus 1 comes in. Suppose we have 128 sectors. This means
        //sector 0, or (128-1)/128, is occupied. Thus, we start at sector 1.
        int double_block_start = total_current_doubly_indirect_sectors == 0 ? 0 : ((total_current_doubly_indirect_sectors-1) / ENTRIES_PER_BLOCK) + 1;

        int ind_double;
        //Append at the end
        for (ind_double = 0; ind_double < total_indirect_blocks_needed; ind_double++) {
            double_indirect_allocation_passed = free_map_allocate(1, &(doubly_indirect_block_entries[double_block_start + ind_double]));
            if (!double_indirect_allocation_passed) {
                break;
            }
        }
        //Rollback if any memory allocations failed
        if (!double_indirect_allocation_passed) {
            int temp;
            for (temp = 0; temp < ind_double; temp++) {
                free_map_release(doubly_indirect_block_entries[double_block_start + temp], 1);
            }
        } else {
            //block_write(fs_device, disk_inode->doubly_indirect, doubly_indirect_block_entries);
            cache_write_at(disk_inode->doubly_indirect, doubly_indirect_block_entries);

            //Fill in "whole" indirect blocks, i.e. indirect blocks where all ENTRIES_PER_BLOCK entries are filled with useful info
            int ind_whole_single;
            for (ind_whole_single = 0; ind_whole_single < num_whole_blocks_needed; ind_whole_single++) {
                block_sector_t singly_indirect_block_entries[ENTRIES_PER_BLOCK];
                int temp;
                for (temp = 0; temp < ENTRIES_PER_BLOCK; temp++) {
                    double_indirect_allocation_passed = free_map_allocate(1, &(singly_indirect_block_entries[temp]));
                    if (!double_indirect_allocation_passed) {
                        break;
                    }
                }
                if (!double_indirect_allocation_passed) {
                    int free_temp;
                    for (free_temp = 0; free_temp < temp; free_temp++) {
                        free_map_release(singly_indirect_block_entries[free_temp], 1);
                    }
                    break;
                } else {
                    //block_write(fs_device, doubly_indirect_block_entries[double_block_start + ind_whole_single], singly_indirect_block_entries);
                    cache_write_at(doubly_indirect_block_entries[double_block_start + ind_whole_single], singly_indirect_block_entries);

                    //Fill in zeroed out data
                    int ind_data;
                    for (ind_data = 0; ind_data < ENTRIES_PER_BLOCK; ind_data ++) {
                        //block_write(fs_device, singly_indirect_block_entries[ind_data], zeros);
                        cache_write_at(singly_indirect_block_entries[ind_data], zeros);
                    }
                }
            }
            
            //Allocate remainder sectors if needed
            if (num_remaining_sectors != 0) {
                static block_sector_t remainder_block_entries[ENTRIES_PER_BLOCK];
                int temp;
                for (temp = 0; temp < num_remaining_sectors; temp++) {
                    double_indirect_allocation_passed = free_map_allocate(1, &(remainder_block_entries[temp]));
                    if (!double_indirect_allocation_passed) {
                        break;
                    }
                }
                if (!double_indirect_allocation_passed) {
                    int free_temp;
                    for (free_temp = 0; free_temp < temp; free_temp++) {
                        free_map_release(remainder_block_entries[free_temp], 1);
                    }
                } else {
                    //block_write(fs_device, doubly_indirect_block_entries[double_block_start + num_whole_blocks_needed], remainder_block_entries);
                    cache_write_at(doubly_indirect_block_entries[double_block_start + num_whole_blocks_needed], remainder_block_entries);

                    //Fill in zeroed out data
                    int ind_data;
                    for (ind_data = 0; ind_data < num_remaining_sectors; ind_data ++) {
                        //block_write(fs_device, remainder_block_entries[ind_data], zeros);
                        cache_write_at(remainder_block_entries[ind_data], zeros);
                    }
                }
            }
        }
    }
    
    //Only update length if memory allocation was successful; will always end up as a multiple of BLOCK_SECTOR_SIZE, but inode_write_at overrides
    //this at the end with the appropriate value
    if (double_indirect_allocation_passed) {
        disk_inode->length = (total_current_sectors + num_sectors_for_doubly_indirect) * BLOCK_SECTOR_SIZE;
    }
    
    return double_indirect_allocation_passed;
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create(block_sector_t sector, off_t length, int32_t is_directory) {
    struct inode_disk *disk_inode = NULL;
    bool success = false;

    ASSERT(length >= 0);

    /* If this assertion fails, the inode structure is not exactly
       one sector in size, and you should fix that. */
    ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

    disk_inode = calloc(1, sizeof *disk_inode);
    if (disk_inode != NULL) {
        size_t sectors = bytes_to_sectors(length);
          
        //Number of direct sectors needed (in total, not a diff)
        int direct_sectors_needed = sectors <= NUM_DIRECT ? sectors : NUM_DIRECT;
        //Number of (singly) indirect sectors needed
        int indirect_sectors_needed = sectors > NUM_DIRECT ? sectors - NUM_DIRECT : 0;
        indirect_sectors_needed = indirect_sectors_needed <= ENTRIES_PER_BLOCK ? indirect_sectors_needed : ENTRIES_PER_BLOCK;
        //Number of doubly indirect sectors needed
        int doubly_indirect_sectors_needed = sectors > NUM_DIRECT + ENTRIES_PER_BLOCK?
            sectors - (NUM_DIRECT + ENTRIES_PER_BLOCK) : 0;
        
        bool direct_allocation_passed = true;
        bool indirect_allocation_passed = true;
        bool doubly_indirect_allocation_passed = true;
        
        if (direct_sectors_needed > 0) {
            //printf("Direct allocation needed\n");
            direct_allocation_passed = inode_direct_append(disk_inode, direct_sectors_needed);
        }
        if (indirect_sectors_needed > 0) {
            //printf("Indirect allocation needed\n");
            indirect_allocation_passed = inode_singly_indirect_append(disk_inode, indirect_sectors_needed);
        }
        if (doubly_indirect_sectors_needed > 0) {
            //printf("Doubly indirect allocation needed\n");
            doubly_indirect_allocation_passed = inode_doubly_indirect_append(disk_inode, doubly_indirect_sectors_needed);
        }
        
        success = direct_allocation_passed && indirect_allocation_passed && doubly_indirect_allocation_passed;
        
        //Only need to write inode to disk if ALL allocations from above succeeded.
        if (success) {
            //printf("All allocations passed\n");
            disk_inode->is_directory = is_directory;
            disk_inode->length = length;
            disk_inode->magic = INODE_MAGIC;  
            //block_write(fs_device, sector, disk_inode);
            cache_write_at(sector, disk_inode);
        }
        free(disk_inode);
    }
    return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open(block_sector_t sector) {
    struct list_elem *e;
    struct inode *inode;

    /* Check whether this inode is already open. */
    for (e = list_begin(&open_inodes); e != list_end(&open_inodes);
            e = list_next(e)) {
        inode = list_entry(e, struct inode, elem);
        if (inode->sector == sector) {
            inode_reopen(inode);
            return inode;
        }
    }

    /* Allocate memory. */
    inode = malloc(sizeof *inode);
    if (inode == NULL)
        return NULL;

    /* Initialize. */
    lock_init(&inode->inode_lock);
    list_push_front(&open_inodes, &inode->elem);
    inode->sector = sector;
    inode->open_cnt = 1;
    inode->deny_write_cnt = 0;
    inode->removed = false;
    //block_read(fs_device, inode->sector, &inode->data);
    cache_read_at(inode->sector, &inode->data);
    
    return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen(struct inode *inode) {
    if (inode != NULL)
        inode->open_cnt++;
    return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber(const struct inode *inode) {
    return inode->sector;
}

static void release_all_entries(struct inode* in) {
    struct inode_disk* disk_inode = &in->data;
    int total_entries = ceil_int(disk_inode->length, BLOCK_SECTOR_SIZE);
    
    //Releases all DATA entries
    int index;
    for (index = 0; index < total_entries; index ++) {
        block_sector_t data_location = byte_to_sector(in, BLOCK_SECTOR_SIZE * index);
        free_map_release(data_location, 1);
    }
    
    //In the case that the singly indirect pointer was used, releases the singly indirect block
    if (total_entries > NUM_DIRECT) {
        free_map_release(disk_inode->indirect, 1);
    }
    
    //In the case that the doubly indirect pointer was used, releases ALL singly indirect blocks and then the doubly indirect block
    if (total_entries > NUM_DIRECT + ENTRIES_PER_BLOCK) {
        int total_single_indirect_blocks = ceil_int(total_entries - (NUM_DIRECT + ENTRIES_PER_BLOCK), ENTRIES_PER_BLOCK);
        
        block_sector_t doubly_indirect_block[BLOCK_SECTOR_SIZE];
        //block_read(fs_device, disk_inode->doubly_indirect, doubly_indirect_block);
        cache_read_at(disk_inode->doubly_indirect, doubly_indirect_block);
        
        int ind_double;
        for (ind_double = 0; ind_double < total_single_indirect_blocks; ind_double++) {
            free_map_release(doubly_indirect_block[ind_double], 1);
        }
        
        free_map_release(disk_inode->doubly_indirect, 1);
    }
    
    //Set new length to 0; all have been freed
    disk_inode->length = 0;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close(struct inode *inode) {
    /* Ignore null pointer. */
    if (inode == NULL)
        return;

    /* Release resources if this was the last opener. */
    if (--inode->open_cnt == 0) {
        /* Remove from inode list and release lock. */
        list_remove(&inode->elem);

        /* Deallocate blocks if removed. */
        if (inode->removed) {
            free_map_release(inode->sector, 1);
            release_all_entries(inode);
        }

        free(inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove(struct inode *inode) {
    ASSERT(inode != NULL);
    inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at(struct inode *inode, void *buffer_, off_t size, off_t offset) {
    uint8_t *buffer = buffer_;
    off_t bytes_read = 0;
    uint8_t *bounce = NULL;

    lock_acquire(&inode->inode_lock);
    while (size > 0) {
        /* Disk sector to read, starting byte offset within sector. */
        block_sector_t sector_idx = byte_to_sector(inode, offset);
        int sector_ofs = offset % BLOCK_SECTOR_SIZE;

        /* Bytes left in inode, bytes left in sector, lesser of the two. */
        off_t inode_left = inode_length(inode) - offset;
        int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
        int min_left = inode_left < sector_left ? inode_left : sector_left;

        /* Number of bytes to actually copy out of this sector. */
        int chunk_size = size < min_left ? size : min_left;
        if (chunk_size <= 0)
            break;

        if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
            /* Read full sector directly into caller's buffer. */
            //block_read(fs_device, sector_idx, buffer + bytes_read);
            cache_read_at(sector_idx, buffer + bytes_read);
        } else {
            /* Read sector into bounce buffer, then partially copy
               into caller's buffer. */
            if (bounce == NULL) {
                bounce = malloc(BLOCK_SECTOR_SIZE);
                if (bounce == NULL)
                    break;
            }
            //block_read(fs_device, sector_idx, bounce);
            cache_read_at (sector_idx, bounce);
            memcpy(buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }

        /* Advance. */
        size -= chunk_size;
        offset += chunk_size;
        bytes_read += chunk_size;
    }
    lock_release(&inode->inode_lock);
    free(bounce);

    return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at(struct inode *inode, const void *buffer_, off_t size,
        off_t offset) {
    const uint8_t *buffer = buffer_;
    off_t bytes_written = 0;
    uint8_t *bounce = NULL;

    if (inode->deny_write_cnt)
        return 0;
    
    lock_acquire(&inode->inode_lock);
    
    //Bool to determine whether to go on to actual writing portion depending on success of memory allocation
    bool proceed = true;
    
    //Need to extend file in this case
    off_t len = inode_length(inode);
    //printf("Current inode length is %d\n", (int) len);
    //printf("Offset plus size is %d\n", (int) (offset + size));

    if (offset + size > len) {
        //One-indexed
        int last_sector_index_after_write = ceil_int(offset + size, BLOCK_SECTOR_SIZE);
        
        //Number of direct sectors needed (in total, not a diff)
        int direct_sectors_needed = last_sector_index_after_write <= NUM_DIRECT ? last_sector_index_after_write : NUM_DIRECT;
        //Number of (singly) indirect sectors needed
        int indirect_sectors_needed = last_sector_index_after_write > NUM_DIRECT ? last_sector_index_after_write - NUM_DIRECT : 0;
        indirect_sectors_needed = indirect_sectors_needed <= ENTRIES_PER_BLOCK ? indirect_sectors_needed : ENTRIES_PER_BLOCK;
        //Number of doubly indirect sectors needed
        int doubly_indirect_sectors_needed = last_sector_index_after_write > NUM_DIRECT + ENTRIES_PER_BLOCK ?
            last_sector_index_after_write - (NUM_DIRECT + ENTRIES_PER_BLOCK) : 0;
        
        //Total number of sectors across all pointers
        int total_current_sectors = ceil_int(len, BLOCK_SECTOR_SIZE);
        
        //Bound on upper end by NUM_DIRECT (lower bound is implicit due to being unable to have len < 0)
        int total_current_direct_sectors = total_current_sectors <= NUM_DIRECT ? total_current_sectors : NUM_DIRECT;
        //Bound on lower end by zero, upper end by ENTRIES_PER_BLOCK, i.e. the number of entries in the indirect block.
        int total_current_indirect_sectors = total_current_sectors - NUM_DIRECT >= 0 ? total_current_sectors - NUM_DIRECT : 0;
        total_current_indirect_sectors = total_current_indirect_sectors <= ENTRIES_PER_BLOCK ? total_current_indirect_sectors : ENTRIES_PER_BLOCK;
        //Bound on lower end by zero, upper end bound due to check inside append call
        int total_current_doubly_indirect_sectors = total_current_sectors - NUM_DIRECT - ENTRIES_PER_BLOCK >= 0 ?
            total_current_sectors - NUM_DIRECT - ENTRIES_PER_BLOCK : 0;
        
        int diff_direct = direct_sectors_needed - total_current_direct_sectors;
        int diff_indirect = indirect_sectors_needed - total_current_indirect_sectors;
        int diff_double_indirect = doubly_indirect_sectors_needed - total_current_doubly_indirect_sectors;
        
        bool direct_passed = true;
        bool indirect_passed = true;
        bool doubly_indirect_passed = true;
        
        //The inode's length may change in these calls (only is successfully allocated), but is overwritten if all succeeded.
        if (diff_direct > 0) {
            //printf("Write direct allocation\n");
            direct_passed = inode_direct_append(&inode->data, diff_direct);
        }
        if (diff_indirect > 0) {
            //printf("Write indirect allocation\n");
            indirect_passed = inode_singly_indirect_append(&inode->data, diff_indirect);
        }
        if (diff_double_indirect > 0) {
            //printf("Write doubly indirect allocation\n");
            doubly_indirect_passed = inode_doubly_indirect_append(&inode->data, diff_double_indirect);
        }     
        
        //We only care about zero padding the last current sector if all of the extra block allocations from previously were executed successfully
        if (direct_passed && indirect_passed && doubly_indirect_passed) {
            block_sector_t last_sector = len == 0 ? byte_to_sector(inode, 0) : byte_to_sector(inode, len-1);
            static char data_buff[BLOCK_SECTOR_SIZE];
            //block_read(fs_device, last_sector, data_buff);
            cache_read_at(last_sector, data_buff);

            int modded_len = len % BLOCK_SECTOR_SIZE;

            //Zero padding WITHIN THE LAST SECTOR ONLY
            int zero_padding = offset - len >= 0 ? offset - len : 0;
            zero_padding = zero_padding > BLOCK_SECTOR_SIZE - modded_len ? BLOCK_SECTOR_SIZE - modded_len : zero_padding;
            
            int index;

            for (index = modded_len; index < modded_len + zero_padding; index ++) {
                data_buff[index] = 0;
            }
            //Write back zeroed out entries
            //block_write(fs_device, last_sector, data_buff);
            cache_write_at(last_sector, data_buff);
            
            //After everything else succeeded, we must update the length again, because the sector allocation calls only update length at
            //multiples of BLOCK_SECTOR_SIZE
            inode->data.length = offset + size;
            //printf("Write length has now been set to %d\n", (int) inode->data.length);

            //Write altered inode_disk back to disk
            cache_write_at(inode->sector, &inode->data);
        }
        else {
            //printf("Allocation failed\n");
            //proceed = false;
            return -1;
        }
    }

    //At this point, even though we didn't actually write any data yet, the file has already been fully extended, and thus all writes will simply
    //just overwrite the zeroed out regions.
    if (proceed) {
        while (size > 0) {
            /* Sector to write, starting byte offset within sector. */
            block_sector_t sector_idx = byte_to_sector(inode, offset);
            int sector_ofs = offset % BLOCK_SECTOR_SIZE;

            /* Bytes left in inode, bytes left in sector, lesser of the two. */
            off_t inode_left = inode_length(inode) - offset;
            int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
            int min_left = inode_left < sector_left ? inode_left : sector_left;

            /* Number of bytes to actually write into this sector. */
            int chunk_size = size < min_left ? size : min_left;
            if (chunk_size <= 0) {
                break;
            }
            //printf("Chunk size is %d\n", chunk_size);

            if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
                /* Write full sector directly to disk. */
                //block_write(fs_device, sector_idx, buffer + bytes_written);
                cache_write_at (sector_idx, (void *) (buffer + bytes_written));
            } else {
                /* We need a bounce buffer. */
                if (bounce == NULL) {
                    bounce = malloc(BLOCK_SECTOR_SIZE);
                    if (bounce == NULL)
                        break;
                }

                /* If the sector contains data before or after the chunk
                   we're writing, then we need to read in the sector
                   first.  Otherwise we start with a sector of all zeros. */
                if (sector_ofs > 0 || chunk_size < sector_left)
                    //block_read(fs_device, sector_idx, bounce);
                    cache_read_at(sector_idx, bounce);
                else
                    memset(bounce, 0, BLOCK_SECTOR_SIZE);
                memcpy(bounce + sector_ofs, buffer + bytes_written, chunk_size);
                //block_write(fs_device, sector_idx, bounce);
                cache_write_at (sector_idx, bounce);
            }

            /* Advance. */
            size -= chunk_size;
            offset += chunk_size;
            bytes_written += chunk_size;
        }
    }
    lock_release(&inode->inode_lock);
    
    free(bounce);
    return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write(struct inode *inode) {
    inode->deny_write_cnt++;
    ASSERT(inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write(struct inode *inode) {
    ASSERT(inode->deny_write_cnt > 0);
    ASSERT(inode->deny_write_cnt <= inode->open_cnt);
    inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length(const struct inode *inode) {
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

//Returns whether the "file" is actually a directory
bool is_dir(struct inode* i) {
    return i->data.is_directory == 1;
}

int inode_open_cnt(struct inode * i) {
    return i->open_cnt;
}

bool inode_is_root(struct inode * inode)
{
    if (inode->sector == ROOT_DIR_SECTOR)
    {
        return true;
    }
    return false;
}