#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include <list.h>


//Struct corresponding to the kernel's view of open files.
struct kernel_file_map_elem {
    struct file* file;
    int num_proc;
    struct list_elem elem;
};

struct list kernel_file_map; /*Maps file structs to inodes*/

void syscall_init (void);




#endif /* userprog/syscall.h */
