
Final Report for Project 2: User Programs
=========================================

## Changes

### Task 1 (Argument Passing):



### Task 2 (Process Operation Syscalls):
### Improvements:

#### In `threads.h`:
```C
struct  wait_status
{
	struct  list_elem elem; /* ‘children’ list element. */
	struct  lock lock; /* Protects me_alive and parent_alive. */
	int me_alive; //init in
	int parent_alive; //init in init_thread()
	tid_t tid; /* Child thread id. */
	int exit_code; /* Child exit code, if dead. */
	int load_status; /* 0 successfully loaded, -1 fail to load */
	struct  semaphore wait_load; /* parent will wait for child's wait_load to wait for child proc to finish loading */
	struct  semaphore dead;
};
struct  process_file_map_elem {
	int fd;
	struct  file* file;
	struct  list_elem elem;
};
struct thread
{
...
#ifdef  USERPROG
/* Owned by userprog/process.c. */
uint32_t  *pagedir; /* Page directory. */
struct  wait_status  *wait_status; /* This process’s completion state. */
struct  list children; /* Completion status of children. */
struct  list process_file_map; /* List of process_file_map_elem that deals with maps file descriptors to file structs */
int next_fd; /*Next file descriptor value to use upon successful syscall to open; ranges from 2 to 128, inclusive*/
//NOTE: next_fd will only be updated on the first call to a file's open, unless that file has been removed
bool holding_filesys_lock;
#endif
...
};
```
#### In `process.h`:
```c
int
process_wait (tid_t child_tid UNUSED)
{
	struct wait_status *tid_wait_status;
	tid_wait_status =  get_tid_wait_status(child_tid);
	int tid_exit_code;
	if (tid_wait_status ==  NULL)
	{
		return  -1;
	}
	else
	{
		sema_down(&tid_wait_status->dead);
		tid_exit_code = tid_wait_status->exit_code;
		list_remove(&tid_wait_status->elem);
		free(tid_wait_status);
		return tid_exit_code;
	}
}

void
process_exit (void)
{
	...
	if (get_all_list_size() <=  2) //only main and idle left
	{
	}
	else
	{
		struct wait_status *ws;
		printf("%s: exit(%d)\n", thread_current ()->name, thread_current()->wait_status->exit_code);
		lock_acquire(&cur->wait_status->lock);
		cur->wait_status->me_alive  =  0;
		if (cur->wait_status->parent_alive  ==  0)
		{
			lock_release(&cur->wait_status->lock);
			free(cur->wait_status);
		}
		else
		{
			sema_up(&cur->wait_status->dead);
			lock_release(&cur->wait_status->lock);
		}

		while(!list_empty(&cur->children))
		{
			ws=list_entry(list_pop_front(&cur->children), struct wait_status, elem);
			lock_acquire(&ws->lock);
			ws->parent_alive  =  0;
			if (ws->me_alive  ==  0)
			{
				lock_release(&ws->lock);
				free(ws);
			}
			else
			{
				lock_release(&ws->lock);
			}
		}
	}
}
```

### Task 3 (File Operation Syscalls):
**1)** The `file_inode_list_elem` struct was eliminated due to two reasons. The first was that a file struct already had direct access to its corresponding inode (simply access the inode as a member variable of the file, i.e. someFile.inode); the second was that there was no need to directly modify inodes, since we could just use methods inside file.c or filesys.c that would (behind the scenes) call inode methods anyways.

**2)** Although not technically a change, a clarification on the usage of the file descriptor to file map (represented as a linked list of `fd_file_list_elem` structs) is probably useful here. Specifically, upon OPENING a file (i.e. during an open syscall), the list would be appended to. The insertion did not require any explicit ordering logic – the new element always went to the back; however, due to the nature of our file descriptor allocation, the resulting list was always sorted.

`create_and_push_back_pfme`, a method inside thread.c, allocated file descriptors according to a `next_fd` member variable inside each thread struct. At the start, `next_fd` is 2, due to the fact that file descriptors 0 and 1 are reserved for stdin and stdout, respectively. Upon successful completion of `create_and_push_back_pfme`, `next_fd` is incremented by 1. `next_fd` achieves a maximum value of 128; upon reaching this point, open syscalls no longer modify the file descriptor file map, and will fail to open a file until at least one file is closed.

**3)** Upon closing a file (via its file descriptor), `next_fd` is decremented by 1, and all file descriptor values greater than the file descriptor that was closed are decreased by 1. This occurs in every close syscall, and although somewhat time consuming, is necessary to maintain simple logic in manipulating `next_fd`. The actual method that does this is `remove_pfme_by_fd`, located inside thread.c. Note that this method also frees the memory allocated for the `fd_file_list_elem` being removed (since it is created via malloc), and also frees the file struct via `file_close`, which allows writes on that file again.

**4)** If a thread is terminated, whether naturally via the exit syscall or via a kernel decision, it will call `process_exit`. Thus, we call `close_all_fd`, a method inside thread.c, from `process_exit`, in order to simulate removing all `fd_file_list_elem` structs associated with the terminated thread (including freeing up of memory).

**5)** All syscalls check memory accesses before actually executing the underlying syscall code, via `access_user_memory` in syscall.c. For example, upon calling sys_create, which takes in a character array and an unsigned value, the location that holds the pointer for the array and the location of the unsigned value must be checked for validity, in addition to a check on the actual place where the character array is stored.

## Reflection

### Task 1
Zewei Ding wrote the initial design for task1, but after discussing with TA, we decided not to use his design.  Yingchun Ma reinplemented task 1 and finished the code for task1. Jingqi Wang wrote the final report for task1.

### Task 2
Jiasheng Qin first wrote the initial design for task2. And Yingchun Ma later inplemented task2 with some improvement as listed above. Yingchun Ma wrote the final report for task2

### Task 3
Jiasheng Qin wrote most of the finalized version for task3, with some edits from the other group members in order to pass the multi-oom test.

### Tests
Yingchun Ma wrote the tests.

In general, one of the strongest points of this project was the completion of tasks in a very timely manner; scheduling was performed very effectively, with group members asking TAs and going to the project party in order to resolve remaining issues. Something that could have been improved might be collaboration, as task completion was (initially) largely isolated and independent.

## Student Testing Report

### Test 1: remove-normal:

**Description:** This test is meant to make sure that the kernel behaves appropriately when a syscall to remove a file . 

**Overview:** The test works by first load tests/userprog/sample.txt to disk, and then remove it. After it has been removed, call open on that file name, open should return -1. Which means that file doesn't exist anymore.

**Output:**:
```
Copying tests/userprog/remove-normal to scratch partition...
Copying ../../tests/userprog/sample.txt to scratch partition...
qemu -hda /tmp/oQrCXGKGhc.dsk -m 4 -net none -nographic -monitor null
PiLo hda1
Loading..........
Kernel command line: -q -f extract run remove-normal
Pintos booting with 4,088 kB RAM...
382 pages available in kernel pool.
382 pages available in user pool.
Calibrating timer...  130,457,600 loops/s.
hda: 5,040 sectors (2 MB), model "QM00001", serial "QEMU HARDDISK"
hda1: 175 sectors (87 kB), Pintos OS kernel (20)
hda2: 4,096 sectors (2 MB), Pintos file system (21)
hda3: 104 sectors (52 kB), Pintos scratch (22)
filesys: using hda2
scratch: using hda3
Formatting file system...done.
Boot complete.
Extracting ustar archive from scratch device into file system...
Putting 'remove-normal' into the file system...
Putting 'sample.txt' into the file system...
Erasing ustar archive...
Executing 'remove-normal':
(remove-normal) begin
(remove-normal) remove sample.txt
(remove-normal) end
remove-normal: exit(0)
Execution of 'remove-normal' complete.
Timer: 59 ticks
Thread: 0 idle ticks, 58 kernel ticks, 1 user ticks
hda2 (filesys): 113 reads, 217 writes
hda3 (scratch): 103 reads, 2 writes
Console: 993 characters output
Keyboard: 0 keys pressed
Exception: 0 page faults
Powering off...
```

**Result:**
```
PASS
```

**Kernel Bugs:** 1) If the kernal didn't call `filesys_remove(filename)` , then it can't remove a file from disk, the test will fail. 2) If the kernel didn't forward the return value of the filesystem call back to the `eax` register, then the test would fail because the return value wouldn't be `true` then `false`.

---

### Test 2: remove-open:

**Description:** When a file is removed (deleted), its blocks are not deallocated until all processes have closed all file descriptors pointing to it. Therefore, a deleted file may still be accessible by processes that have it open. This test removes a file that is opened, and found that we can still access to it. 

**Overview:** First open `sample.txt`, then remove it. And then call `check_file_handle`, at last close it.
**Output:**:
```C
Copying tests/userprog/remove-open to scratch partition...
Copying ../../tests/userprog/sample.txt to scratch partition...
qemu -hda /tmp/LClFCagyX6.dsk -m 4 -net none -nographic -monitor null
PiLo hda1
Loading..........
Kernel command line: -q -f extract run remove-open
Pintos booting with 4,088 kB RAM...
382 pages available in kernel pool.
382 pages available in user pool.
Calibrating timer...  78,540,800 loops/s.
hda: 5,040 sectors (2 MB), model "QM00001", serial "QEMU HARDDISK"
hda1: 175 sectors (87 kB), Pintos OS kernel (20)
hda2: 4,096 sectors (2 MB), Pintos file system (21)
hda3: 105 sectors (52 kB), Pintos scratch (22)
filesys: using hda2
scratch: using hda3
Formatting file system...done.
Boot complete.
Extracting ustar archive from scratch device into file system...
Putting 'remove-open' into the file system...
Putting 'sample.txt' into the file system...
Erasing ustar archive...
Executing 'remove-open':
(remove-open) begin
(remove-open) remove sample.txt
(remove-open) verified contents of "sample.txt"
(remove-open) close "sample.txt"
(remove-open) end
remove-open: exit(0)
Execution of 'remove-open' complete.
Timer: 56 ticks
Thread: 1 idle ticks, 54 kernel ticks, 1 user ticks
hda2 (filesys): 100 reads, 219 writes
hda3 (scratch): 104 reads, 2 writes
Console: 1057 characters output
Keyboard: 0 keys pressed
Exception: 0 page faults
Powering off...
```

**Result:**
```C
PASS
```

**Kernel Bugs:** 1) When a file is removed, if the kernal deallocat its blocks before all processes have closed all file descriptors pointing to it, this test will fail. 2) If the kernal prevent the file which is opened from removing, this test will also fail.

---

### Review:

Writing the actual source (`.c`) file is very easy.  However, the `.ck` file it can be hard to understand the syntax and know what things should be printed by the OS.  Changing the code in the `Make.tests` file was easy, but could be easy to make a mistake if you accidently enter the wrong things.  I learned how the `make check` works by looking at the makefiles when writing tests.

