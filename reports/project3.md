Final Report for Project 3: File System
=======================================

## Changes

### Task 1 (Buffer Cache):

### Task 2 (Extensible Files):
**1)** For synchronization purposes, as per a suggestion from the TA, we changed the locking scheme from a list of locks
corresponding to each block in a file (corresponding to a particular inode) to a single lock for each inode. Essentially, the
reasoning was that a write to a particular file should be locked on a file-wide scale - that is, across ALL blocks in the file:
after all, suppose that two people were writing to the same file, one at the start of the file, one near the end; if we used the
lock list scheme, these two people would be allowed to concurrently write, and the ultimate result of the file might be
a jumbled mess with the start not connecting to the end at all.

**2)** The second change was regarding memory exhaustion - we realized that the correct method to check was not `block_register` but
rather `free_map_allocate`. If this method failed during any chain of memory allocations, we would need to roll back any SUCCESSFUL
allocations along the way, by calling `free_map_release`. For example, if we wanted to allocate 5 new blocks, but allocation of the
fifth block failed, we would need to "roll back" blocks 1 through 4.

**3)** Not a technical change, but instead of using absolute numeric values, we decided to use macros instead for consistency.
For example, instead of using 12, we used `NUM_DIRECT`, and defined `NUM_DIRECT` to be 12 in `inode.c`.

### Task 3 (Subdirectories):

----------------------

## Reflection

### Task 1

### Task 2
Jiasheng wrote most of the final implementation of extensible files, and performed a large portion of the integration with tasks
1 and 3. He also wrote the initial design doc for task 2. Yinchun fixed a particularly nasty bug involving the integration
aspect that had been preventing us from passing 3 tests.

### Task 3


**Positives and potential improvements:** Tasks were done efficiently and very promptly. However, the integration aspect could have
been simplified with better communication among group members. In addition, perhaps going to office hours earlier to address the
three final failing test cases might have saved a lot of time.

----------------------

**TESTING REPORT:**

**_Test 1_**

**1)** In this test, we verified that the hit rate of a cold cache on a given file is lower than the hit rate of the cache after it has read the file once.

**2)** First, we create a file, "test1", with size 500 bytes (filled initially with arbitrary values), and then open that file. Resetting the buffer cache takes place after this due
to the nature of our buffer cache implementation: when a file is opened, the contents are already written to cache; however, this would defeat the spirit of our initial read
not knowing anything about the file. Our reads perform reads of 64 bytes at a time (but since the file size isn't a multiple of 500 bytes, the last read is 52 bytes long). This is
a total of 8 reads. Then, we close the file, open it again, and reread it. Again, this is 8 reads. Note that since the file size is 500 bytes, this is within the range of a SINGLE
block. Therefore, there will only be a single compulsory miss the first time. This compulsory miss disappears during the non-cold read, however. Thus, the hit rate improved from
7/8 hits to 8/8 hits

**3)**
Raw output file:

``` 
Copying tests/filesys/extended/cache-hit-rate to scratch partition...
Copying tests/filesys/extended/tar to scratch partition...
qemu -hda /tmp/JzA41lrr3a.dsk -hdb tmp.dsk -m 4 -net none -nographic -monitor null
PiLo hda1
Loading...........
Kernel command line: -q -f extract run cache-hit-rate
Pintos booting with 4,088 kB RAM...
382 pages available in kernel pool.
382 pages available in user pool.
Calibrating timer...  419,020,800 loops/s.
hda: 1,008 sectors (504 kB), model "QM00001", serial "QEMU HARDDISK"
hda1: 185 sectors (92 kB), Pintos OS kernel (20)
hda2: 239 sectors (119 kB), Pintos scratch (22)
hdb: 5,040 sectors (2 MB), model "QM00002", serial "QEMU HARDDISK"
hdb1: 4,096 sectors (2 MB), Pintos file system (21)
filesys: using hdb1
scratch: using hda2
Formatting file system...done.
Boot complete.
Extracting ustar archive from scratch device into file system...
Putting 'cache-hit-rate' into the file system...
Putting 'tar' into the file system...
Erasing ustar archive...
Executing 'cache-hit-rate':
(cache-hit-rate) begin
(cache-hit-rate) create "test1"
(cache-hit-rate) open "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) open "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) Hot hit rate is 8
(cache-hit-rate) Cold hit rate is 7
(cache-hit-rate) Verifying that cache hit rate has improved...
(cache-hit-rate) end
cache-hit-rate: exit(0)
Execution of 'cache-hit-rate' complete.
Timer: 64 ticks
Thread: 0 idle ticks, 60 kernel ticks, 4 user ticks
hdb1 (filesys): 518 reads, 485 writes
hda2 (scratch): 238 reads, 2 writes
Console: 1735 characters output
Keyboard: 0 keys pressed
Exception: 0 page faults
Powering off...
```

Raw results:

```
PASS
```

**4)** Note that our syscall `buffer_cache_reset` does not simply reinitialize the buffer cache. It actually first flushes
the cache, then reinitializes it. If not for the first step, consider the case where I attempt to read from the same file
again. Obviously, we would not find anything in the cache. However, because flushing would not have occurred, we would
also not find anything on disk either, which would result in a page fault - the file isn't present anywhere.

The second potential bug is an ordering one. The syscall `most_recent_cache_search` returns if the **most recent** cache
search (whether a read or a write) hit in the cache. If we had called `most_recent_cache_search` BEFORE calling read, then
our hit rate wouldve been skewed towards less hits: after all, the first search would be guaranteed to miss (since
the boolean behind the syscall starts off as false by default), and the second search would be the compulsory miss. Hence,
our hit rates would be 6/8 and 7/8 for the cold and hot caches, respectively.


**_Test 2_**

**1)** In this test, we checked that our buffer cache was able to coalesce writes to the same sector efficiently, such that
the total number of device writes (writes to disk) was minimized over a byte-by-byte write then read of a large file.

**2)** In our implementation, we created then opened a file with size 64 KB (65536 bytes), then wrote the letter a into it
repeatedly (byte by byte). After filling up the file, we seeked the position back to 0, then started reading from the
beginning. At the end, we issued a syscall (`get_write_cnt`) to check for the number of device writes, making sure it was within one order of
magnitude from 128 (i.e. at least 12, at most 1280). As stated in the project manual, it is supposed to be around this range
because the size of the file is 128 blocks; all of the device writes will be due to cache eviction after the cache runs out of
space first from the writes, at which point it evicts the starting portions of the file, and then ends up having to evict the
newer portions during the reading process, which asks for the initial portions of the file first, and finally evicting the
older portions of the file in order to bring in the newer portions for the read.

**3)**
Raw output file:

```
Copying tests/filesys/extended/write-coalesce to scratch partition...
Copying tests/filesys/extended/tar to scratch partition...
qemu -hda /tmp/vBjrgsxjWr.dsk -hdb tmp.dsk -m 4 -net none -nographic -monitor null
PiLo hda1
Loading...........
Kernel command line: -q -f extract run write-coalesce
Pintos booting with 4,088 kB RAM...
382 pages available in kernel pool.
382 pages available in user pool.
Calibrating timer...  419,020,800 loops/s.
hda: 1,008 sectors (504 kB), model "QM00001", serial "QEMU HARDDISK"
hda1: 185 sectors (92 kB), Pintos OS kernel (20)
hda2: 237 sectors (118 kB), Pintos scratch (22)
hdb: 5,040 sectors (2 MB), model "QM00002", serial "QEMU HARDDISK"
hdb1: 4,096 sectors (2 MB), Pintos file system (21)
filesys: using hdb1
scratch: using hda2
Formatting file system...done.
Boot complete.
Extracting ustar archive from scratch device into file system...
Putting 'write-coalesce' into the file system...
Putting 'tar' into the file system...
Erasing ustar archive...
Executing 'write-coalesce':
(write-coalesce) begin
(write-coalesce) create "test2"
(write-coalesce) open "test2"
(write-coalesce) writing done
(write-coalesce) resetting file position
(write-coalesce) reading done
(write-coalesce) Verifying order of device writes...
(write-coalesce) end
write-coalesce: exit(0)
Execution of 'write-coalesce' complete.
Timer: 240 ticks
Thread: 0 idle ticks, 60 kernel ticks, 180 user ticks
hdb1 (filesys): 898 reads, 737 writes
hda2 (scratch): 236 reads, 2 writes
Console: 1248 characters output
Keyboard: 0 keys pressed
Exception: 0 page faults
Powering off...
```

Raw results:

```
PASS
```

**4)** One bug that we actually encountered while writing this test was trying to make the array to read the file 65536 bytes
long (in order to match the file size). However, this character array ended up intruding into some protected space, and would
result in a page fault. Thus, we decided to just make the character array a single slot, and override the 0th index repeatedly
during our reads. After all, the purpose is not to have a buffer filled with the contents of the file, but rather to check
the number of device writes.

Another potential issue might be if we had issued an open then close pair of syscalls instead of a seek to 0. Recall that in
our implementation, open syscalls actually fill in our cache. Hence, the number of device writes would end up being artificially
inflated, since opening such a large file would result in extra cache evictions after running out of space during the opening
process.

-----------------------------

**Testing Experience:** This was Jiasheng's first time writing test cases, and even though he knew nothing about Perl, it was
pretty quick to catch on - basically, we supplied an intended output and compared it with our actual one. However, one issue was
that he had to write several extra syscalls, which required changes across multiple files, in order to actually extract the
information that he needed. This could have been mitigated if the block struct was stated in the header file (instead of the C
source file), since as it stands, trying to do fs_device->write_cnt results in a compiler error regarding "incomplete types"
(since it doesn't know what the actual contents of the struct are only based on the header file). Of course, the next biggest
timesaver would be if test cases could directly access kernel methods (like directly calling cache_flush() inside inode.c
instead of having to make a convoluted syscall to do so), but it makes sense why this layer of separation exists: after all, we're
users, not the kernel.

As for some minor gripes, perhaps there could be more detailed instructions on the syntax of Perl, and maybe a manual on the
different types of verification macros (like CHECK) and methods (like fail). Otherwise, writing these tests has led me to realize
that test writing can be just as difficult as writing actual code, and certain bugs (such as the character array bug mentioned
for the second test) can be really difficult to pinpoint.
