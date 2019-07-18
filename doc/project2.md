Design Document for Project 2: User Programs
============================================

## Group Members

* Jiasheng Qin <jqin0713@berkeley.edu>
* FirstName LastName <email@domain.example>
* FirstName LastName <email@domain.example>
* Zewei Ding <ding.zew@berkeley.edu>

Replace this text with your design document.

Task 1:

**Data Structure**

`char** argv[]`
`int argc`
We need an array `argv[]` to record the result of parsed input. Also, we need a counter `argc` to record the size of the array.

**Algorithm**

Once we get the user input, we need to iterate the input. 

First, we need to deduplicate heading, tailing and continuous space.

```C
// record the start of the input string
char* slow;
int j = 0;
char* input;

/* 
   if the current char is space 
    1. it is the head of the input string
    2. it is no the head of the string and the prev char is also space
   we don't need to copy current char
*/
for (i = 0; i < len; i++) {
  if (*(input + i) == ' ' && (i == 0 || *(input + i - 1) == ' ')) {
     continue;
  } 
  *(slow + j) = *(input + i);
  j++;
}

// check the tail of the input is space or not. If the tail of the input is space, remove it. 
```

Second, we need to count the number of command, and save each command on `argv[]`

```C
/*
  iterate the input and find each word. Then copy the word to the argv[]
*/

int slow;
int j = 0;

for (int i = 0; i < len; i++) {
    // Find the starting point of each word
    if (*(input + i) != ' ' && (i == 0 || *(input + i - 1) == ' ')) {
        slow = i;
    }
    // Find the ending point of each word and copy to argv[]
    if (*(input + i) != ' ' && (i == len - 1 || *(input + i + 1) == ' ')) {
        
        strlcpy(argv[j], input[slow], i - slow + 1);
    }
}
```

Third, if the user stack is not enough for the user program, we need to stop execute the user program and alert user about the info. We can get stack info from `pagedir_create()`

Additionally, we could set a size for user input. if the `argc` is larger than certain number, we just return.

**Synchronization**

We need to folk a child process to handle the user input, then pass the parameters to `process_execute()`. 

**Rationale**

User only need to input the name of executable to, which is `argv[0]`, to make them run. And user also needs to be able to input several command with flag to run shell command-line. Once succeed, `process_execute()` will call `thread_create()` with `start_process()` to initiate a new process. The input type of user is STDIN. Therefore, we need to parse the input and allocate the information.

----------------------------------------------

Task 2:

**Data structures:**
Two instance variables will be added to the thread struct inside thread.h. 

The first is a list of threads, which will serve two purposes. Initially, when a process (and thus its corresponding thread) is created (successfully) via an exec() syscall, the current thread running (the “calling process”) will add that child process to its list. This will later be used for wait in order to determine the validity of waiting on a particular child.

The second is a 32-bit unsigned integer pointer (type uint32_t*). This value will store the page directory for the current process, and is obtained by reading the return value of pagedir_create whenever it is called to create stack space for a user process. This value will be used later for pagedir_get_page in order to help manage pointer security.

The following are the new instance variables in C:

`struct list child_threads;`

`uint32* page_dir;`

There will also be a new global variable inside thread.h, in the form of a (user-defined) map. This map will map thread tid’s to their exit status, and will only be appended to when a thread is exiting.

`struct map tid_to_exit_status_map;`

------------------------------------------

**Algorithms:**

The syscalls that need to be implemented are described as followed:

A)	Halt: calls `shutdown_power_off()` [inside devices/shutdown.h]

B)	Exec: calls `process_execute()` as described in task 1.

C)	Wait: procedure as dictated below.

**Scenario:** Suppose process A, corresponding to thread A, wants to wait on child process B, corresponding to thread B and pid Pb. 

**Parent side:** First, thread A’s `child_threads` is checked for a thread with pid Pb. Notice that the only scenario in which thread B is included in A’s `child_threads` is if it is a direct child of thread A: recall that an exec() syscall from process A that successfully generates process B will add thread B to A’s `child_threads` – if the exec call fails, or if B later generates a grandchild (from A’s perspective) process, no new entries are added to A’s `child_threads` list. 

If the check for presence inside `child_threads` fails, the call to wait immediately returns -1. Otherwise, we perform a busy wait for thread B to terminate, currently planned to be in the form of `while (!contains_key(tid_to_exit_status_map, Pb));`. As soon as the map entry is updated, we know that the child has finished its exit process, and thus we no longer have to wait. Afterwards, we remove thread B from thread A’s `child_threads` list, and then return the appropriate exit status.

The purpose of the removal is to make sure that a process cannot call wait on a child more than once, as specified in the project manual.

**Child side:** Regardless of how thread B exits, it MUST append to `tid_to_exit_status_map`: if it was terminated via an exit syscall, it should append (Pb, Pb) to the map. Otherwise, it should append (Pb, -1) to the map. This appending should be performed after all relevant memory corresponding to the about-to-exit thread is freed – that is, it should occur at the very end of exiting.

This is to ensure that as soon as the append call terminates, the thread no longer has anything to do, and thus the parent thread appropriately waited for the entire termination process to conclude.

D)	Practice: retrieves the first argument (args[1]), adds 1 to it, and prints out the sum. There will need to be a check to make sure the first argument is an int, but otherwise the logic is simple.

Managing pointer security will be implemented using the first approach dictated inside the project manual: namely, checking the pointer specified by the user before dereferencing it. Regardless of whether the pointer checked is the stack pointer (obtained by loading esp) or a pointer used as a syscall argument, the following checks will apply:

A)	First, the pointer is checked for equivalence to NULL (that is, if it’s a null pointer). If so, we terminate the user process by issuing an exit syscall.

B)	Next, the pointer is checked for having a valid mapping using the following. First, we use `pagedir_get_page` on the thread’s `page_dir` instance variable (as dictated inside the data structures section), while also passing in the pointer of interest as the `uaddr` argument.

The current implementation of `pagedir_get_page` already “returns … a null pointer if uaddr is unmapped”. That is, we can check the return value of `pagedir_get_page` and if it’s NULL, we can terminate the process in a similar manner to case A.

However, we must also account for the “boundary condition” specified in the project manual:  “It may be the case that a 4-byte memory region (like a 32-bit integer) consists of 2 bytes of valid memory and 2 bytes of invalid memory, if the memory lies on a page boundary.”

In such a scenario, we should check that the value of pointer + 4 is less than `PGSIZE`, which itself is provided inside vaddr.h. Note that this is because the address of pointer is the START of the 4-byte segment, so by adding 4, we are checking if the address of the END of the segment is still within page bounds. If this inequality is false, we will terminate the process.

C)	Lastly, to check that we did not intrude into the kernel address space, we can simply verify that the value returned by `pagedir_get_page` (which returns a kernel virtual memory address), is not between `PHYS_BASE` and the address corresponding to 4GB (Section 3.1.6 inside the project manual describes this region as reserved specifically for the kernel). If we did intrude, we will terminate the process.

---------------------------------------------

**Synchronization:**

The exit syscall does not need any additional synchronization features.

The halt syscall shuts down the whole system, so synchronization is not necessary – all threads will be terminated.

The practice syscall does not require synchronization, since the provided integer argument is not shared with any other thread; thus, the return value is fixed as soon as the syscall is received and parsed.

Synchronization for the exec syscall will occur inside the implementation of task 1 (that is, inside the body of `process_execute` rather than inside `syscall_handler`).

Synchronization for the wait syscall is miniscule. The only instance variable relevant to waiting is `child_threads`, which can only be altered by the thread that it describes (that is, thread X’s `child_threads` can only be altered by thread X). `tid_to_exit_status_map` is alterable by any thread, but order of appending is completely irrelevant; however, we do need to make sure that a tid – exit status pair is either completely updated in the map, or not present at all, since we only check the key’s presence during our wait; this could be done inside our implementation of map’s “append” method, in which we make it atomic (for example by disabling interrupts for the duration of the function).

Synchronization for memory security will disable interrupts before address checking begins and reset interrupts to their original level when all usages of that address (e.g. later in the form of a read or write) end. Essentially, the operation will become atomic – this is because we cannot afford to have a situation where we previous checked that a memory location was MAPPED but then due to context switching to another thread, later became UNMAPPED (perhaps due to freeing up of stack space after a function return). If we passed the initial validity check, context switched and allowed it to become invalid, and then returned to the original context to do a read or write to unmapped memory, this would be a dangerous security failure.

-----------------------------------

**Rationale:**

I originally considered a signaling system between the parent and child, in which I had the child manually remove itself from the parent’s `child_threads` list upon exiting – the parent would check for the presence of the child in its list instead of looking at a global map. In addition, the child would then wait for the parent to send acknowledgement that it had read the child’s exit status, which was another instance variable was supposed to be inside the thread struct (as of the current implementation, it has been moved to be the “value” for each key-value pair inside the global map). Only after the child had received acknowledgement would it free up all of its memory.

However, this implementation was much more complicated in terms of the amount of variables needed, and there was also the issue of the wait method not truly waiting for the end of the child process’s termination – after all, the child still needed to busy wait on the parent’s acknowledgement, so theoretically, the parent could have finished waiting before the child even terminated.

I chose the current method for its relative simplicitly, and the fact that there was a lot of flexibility in designing my own struct (the global map). Code should not be that complicated for designing the map; essentially, it will be a list of pairs, accessible in linear runtime; I can just borrow the list implementation provided to us and pass in a “pair” struct for each entry.

--------------------------------------

Task 3:

--------------------------------------

**Additional Questions:**

**1)** One such test is sc-bad-sp.c. The offending line of code (line 18) is:

`asm volatile ("movl $.-(64*1024*1024), %esp; int $0x30");`

This code attempts to make a syscall with `esp` at an offset of -64*1024*1024 (bytes). However, notice that this is located below the code segment of the stack (specifically 64MB below) and thus the syscall should fail.
 

**2)** One such test is sc-boundary-2.c. The offending line of code (line 20) is:

`asm volatile ("movl %0, %%esp; int $0x30" : : "g" (p));`

The source of the error is what p is defined as (on line 15): 

`int *p = (int *) ((char *) get_boundary_area () - 7);`

Since we want to obtain a chunk of 8 bytes (used for the arguments of the syscall), the last byte will dangle over into a neighboring page (the first 7 bytes will still be valid), and is thus located in invalid memory.
 

**3)** One requirement of the project is that in the wait syscall, a parent process can only call wait on a direct child process. Although `wait-bad-pid` tests that an invalid pid cannot be used to wait on (it chooses a fixed pid that is guaranteed to not belong to any process), we also need to make sure that even a valid pid, corresponding to an existing process, also cannot be waited on if that existing process is not a child process. To make such a test, we could have something like: 

-- Process A calls exec to spawn process B

-- Process B calls exec to spawn process C

-- Process A, B, and C have pid values Pa, Pb, and Pc, respectively

In such a scenario, if process A calls `wait(Pc)`, the wait call should return -1 upon realizing that process C is not one of A’s direct children.


**GDB Questions**

**1)** The thread executing the current function is the main thread (name is “main”). Its address on the stack appears to be 0xc000ee0c

There is only one other thread, the idle thread (name is “idle”). Copying the idle thread’s struct gives:

`{tid = 2, status = THREAD_BLOCKED, name = "idle", '\000' <repeats 11 times>, stack = 0xc0104f34 "", priority = 0, allelem = {prev = 0xc000e020, next = 0xc0034b58 <all_list+8>}, elem = {prev = 0xc0034b60 <ready_list>, next = 0xc0034b68 <ready_list+8>}, pagedir = 0x0, magic = 3446325067}`

**2)** Copy pasting:

`#0  process_execute (file_name=file_name@entry=0xc0007d50 "args-none") at ../../userprog/process.c:32`

`#1  0xc002025e in run_task (argv=0xc0034a0c <argv+12>) at ../../threads/init.c:288`

`#2  0xc00208e4 in run_actions (argv=0xc0034a0c <argv+12>) at ../../threads/init.c:340`

`#3  main () at ../../threads/init.c:133`

The corresponding lines of C code are (first three are all in the init.c file):

#3’s line number has (inside the main method): `run_actions (argv);`

#2’s line number has (inside the run_action method): `a->function (argv);`

#1’s line number has (inside the run_task method): `process_wait (process_execute (task));`

#0’s line number corresponds to the beginning of the `process_execute` method inside the process.c file.

**3)** The name of the thread is "args-none\000\000\000\000\000\000". Its address on the stack is 0xc010afd4. Other threads present include the main thread and the idle thread:

Main thread: `{tid = 1, status = THREAD_BLOCKED, name = "main", '\000' <repeats 11 times>, stack = 0xc000eebc "\001", priority = 31, allelem = {prev = 0xc0034b50 <all_list>, next = 0xc0104020}, elem = {prev = 0xc0036554 <temporary+4>, next = 0xc003655c <temporary+12>}, pagedir = 0x0, magic = 3446325067}`

Idle thread: `{tid = 2, status = THREAD_BLOCKED, name = "idle", '\000' <repeats 11 times>, stack = 0xc0104f34 "", priority = 0, allelem = {prev = 0xc000e020, next = 0xc010a020}, elem = {prev = 0xc0034b60 <ready_list>, next = 0xc0034b68 <ready_list+8>}, pagedir = 0x0, magic = 3446325067}`

**4)** The thread is created at line 424 (based on the skeleton code) of thread.c, located in the `kernel_thread` method. The line is:

`function(aux);`

The function passed in to `kernel_thread` was the `start_process` method, hence the line above creates the thread that runs `start_process`.

**5)** The faulting address is 0x0804870c

**6)** Copy paste:

`_start (argc=<error reading variable: can't compute CFA for this frame>, argv=<error reading variable: can't compute CFA for this frame>) at ../../lib/user/entry.c:9`

**7)** The issue is that it cannot compute the canonical frame address (CFA) for the frame associated with the call to `_start` inside entry.c. Thus, although it knows the variables argc and argv exist, it does not know how to compute their addresses, hence resulting in a page fault.

