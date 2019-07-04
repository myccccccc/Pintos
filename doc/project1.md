Design Document for Project 1: Threads
======================================

## Group Members

* Jiasheng Qin <jqin0713@berkeley.edu>
* Jingqi Wang <jackiewang@berkeley.edu>
* Yingchun Ma <mayingchun@berkeley.edu>
* Zewei DIng <ding.zew@berkeley.edu>

---

## Task 1: Efficient Alarm Clock

### Data structures and functions

#### In `threads.h`:
```C
struct thread {
    ...
    /* Record how many ticks does the thread have to sleep, initialized to be zero in init_thread*/
    int64_t ticks_sleep;
    ...
}

/* if ticks_sleep runs out, unblock the thread, else ticks_sleep minus one */
void  wakeup_sleeping_thread (struct thread *t, void  *aux UNUSED);
```
#### In `timer.c`:
```C
void
timer_sleep (int64_t ticks)
{
	...
	current_thread->ticks_sleep  = ticks;
	...
	thread_block ();
	...
}
static  void
timer_interrupt (struct intr_frame *args UNUSED)
{
	thread_foreach (wakeup_sleeping_thread, NULL);
	...
}
```

### Algorithms

#### Set the number of  ticks should the thread sleep
In `timer_sleep(...)` we set  `current_thread->ticks_sleep()`  to be `ticks` and than block the thread.

#### Wake up a thread
Since `timer_interrupt` happens every tick, So in the `timer_interrupt (...)`, we use `thread_foreach (...)` to call `wakeup_sleeping_thread(...)` on every thread. If the thread's `ticks_sleep` is larger than 0, we do `ticks_sleep--`. And if `ticks_sleep` is now `0`, we unblock the thread.

### Synchronization

Before we do `thread_block()` in `timer_sleep(...)` we need to do `intr_disable()` , and do `intr_set_level (old_level)` after that. Because this function must be called with interrupts turned off.

### Rationale

Our design is easy to conceptualize, it will only require a little bit amount of code, we only add one member to struct thread. And we didn't add any globle variable. To put the thread in to sleep will only take constant time. To wake up a thread, we need to iterate through all the threads which will only take linear time.

---
## Task 2: Priority Scheduler

### Data structures and functions:

For the main body of the priority scheduler, I plan to use a 64 slot array corresponding to the 54 priority values possible for each thread *(with indices 0 to 63 all corresponding to the specific priority value - i.e., the slot in the array corresponds to the highest priority elements)*. Each slot points to a list of all `threads/jobs` *(one-to-one in this case, so I will use the two terms synonymously)* with that particular priority. Specifically, the data structure will be of form

```C
struct priorityScheduler {
    struct list* allJobs;
    int topPriority;
}
```

Inside the semaphore and condition structs, waiters will be changed from a list to another struct identical to priorityScheduler in its format:

```C
struct waitScheduler {
     struct list* waitingJobs;
     int topPriority;
}
```

It will operate in the same manner as priorityScheduler as described in Algorithms, but will be used for determining the next waiting thread to select based on priority.

Another change will be one made to the thread struct. Now, threads will have two extra variables: effective priority and a list of priority donation recipients. 

Effective priority will be used during the priority donation process, and will initially be equal to priority. In addition, any checks on priority will use effectivePriority instead now *(anywhere in the code where priority is currently checked instead will be replaced accordingly)*; priority’s **ONLY** use is as a **BASE** to restore effectivePriority back to its original value.

donationRecipients is a list of all threads that have received priority donation from this particular thread. It is used in order to correctly implement `nested/recursive` donation.

```C
Struct thread {
….<variables already present>...
    int effectivePriority;
    struct list donationRecipients;
}
```


### Algorithms:

Regarding the priority scheduler, whenever a job with a particular priority requests CPU usage *(i.e. threads that have the THREAD_READY state)*, it will be added to the back of the list with the appropriate priority. The variable topPriority has a default value of -1, and will be used to keep track of the highest priority for which there is at least one job *(i.e. the corresponding job list is non-empty)*.

If all jobs are finished *(at every single priority level)*, topPriority will return to having a value of -1. Otherwise, computing the next thread to execute will essentially follow the format of
`list_pop_front(&priorityScheduler.allJobs[priorityScheduler.topPriority])`. If a job is added with higher priority than the current `topPriority`, `topPriority` will be replaced with that value. If the last job at a certain priority level is popped off, `topPriority` is changed to become the next highest priority level.

Regarding lock acquisition and release, to ensure that higher priority threads on a lock’s waiting list can acquire and release that lock first *(both must be in the same order, since locks cannot be re-acquired until the occupying thread releases that lock)*, we will change the waiting list structure inside semaphore structure. Since locks do not directly contain a waiting list - instead, they have a semaphore member - changing how semaphores add threads to their waiting list will successfully change how locks operate as well.

Specifically, we will mimic the `priorityScheduler` behavior for our `waitScheduler`: whenever a thread with priority P wants to be added to the semaphore's waitlist, we add it to the back of the list corresponding to priority P *(changing topPriority if needed)*. This will replace the simple `list_push_back` operation currently inside the `sema_down` method. Similarly, in `sema_up`, we will have `list_pop_front` replaced with popping the list corresponding to the highest priority.

Such a process guarantees that semaphores *(and locks by extension)* will respect higher priority threads when selecting the next one. We can replicate this strategy for conditions, as the reasoning is similar. Changes will be made inside cond_signal and cond_wait, replacing `list_pop_front` and `list_push_back`, respectively, into the appropriate operations *(i.e. operations on particular lists of the wait scheduler)*.

No code has to be changed inside `lock_acquire` or `lock_release` for high-priority preference, as these methods do not deal with the waiting lists directly.

For priority donation, `lock_acquire` and `lock_release` will be changed. First, we check if the lock is currently being held: `lock->holder == NULL`. If the equality holds, then the lock is currently not being held, so no priority donation is necessary. If the equality fails, then we will check if `lock->holder.effectivePriority <= thread_current().effectivePriority`. These two nested checks will be made **BEFORE** the call to `sema_down`. If the inequality holds, then priority donation will take place: `lock->holder.effectivePriority = thread_current().effectivePriority`.

Notice that the inequality uses the current lock holder’s effective priority: this is because priority donation can occur multiple times, so the current lock holder’s effective priority may also occur potentially many times. Since the base priority never changes, it is insufficient to be used during comparison. In addition, nested donations are also possible, which is why effective priority is also used for the current thread. Specifically, consider the case of when there are three threads, A, B, and C. If A has priority 3, B 2, and C 1, then consider the case where B has a lock L1 that A desires, so A donates its priority to B. However, B wants a lock L2 that C currently holds, so when it donates its priority to C, it is not priority 2 but rather priority 3.

There is another scenario, that requires the use of the `donationRecipients` list: what if A donates its priority to B after B donates to C? In this case, C’s priority should ultimately be raised to 3; that is to say, the order of donation does not matter - only the existence of such donations is relevant. Thus, to recursively donate priorities, we would implement a process with pseudocode similar to the following. Notice that this results in a depth-first traversal, but the order does not matter - all effective priorities will be updated to `targetPriority` by the end. Before calling this, we would have to make sure B’s effective priority is actually lower than A’s, because otherwise we wouldn’t need to donate to begin with.

```C
void changeChildrenPriorities (struct thread* t, int targetPriority) {
     t->effectivePriority = targetPriority;
     for (struct thread c in t->donationRecipients) {
           changeChildrenPriorities(&c, targetPriority);
     }
}
```

Restoring priorities is relatively simple in this implementation. Inside `lock_release`, directly after the assertions, we can perform: `lock->holder.effectivePriority = lock->holder.priority`. The key point is that priority itself never changes, so we can be assured that this will always restore to the original value regardless of how many donations have occurred.

However, of note is that when a lock is about to be released, we must add code such that the donationRecipients of **EVERY** thread in the queue for that particular lock removes the previous lock-holder. That is, consider the case that Thread A holds but is about to release Lock A. In queue for Lock A are threads B, C, and D. In each of these, we would have to check if the donation recipients included thread A. If so, it would need to be removed; this is to ensure future priority donations don’t donate to A again unless it acquires a desired lock.

The reason that nested recursion is accounted for in this scenario is because higher-level (“indirect”) donors are not directly attached to the final recipient thread. As long as somewhere down the line, one of their recipients that IS directly attached to the final recipient thread breaks off its connections, all threads at higher levels also lose connection to the final recipient. This code could go directly after the effective priority setting described previously. 
`Lock_try_acquire` will undergo similar changes to lock_acquire, with the conditional check-and-sets occurring in the case of !success *(i.e. we add an else case and put the conditions there)*.

### Synchronization:

1. Priority scheduler access: the scheduler must maintain proper ordering in the various lists. We can use a lock to achieve this; for example, in `thread_unblock` inside `thread.c`, we currently have `list_push_back` being called. After adapting this to our priority scheduler model, we could issue a `lock_acquire` before and a `lock_release` afterwards. This way, even if multiple threads want to make changes to the priority scheduler, they must go in the order in which the threads were unblocked. Both pushing **AND** popping must be gated off.

2. Wait scheduler access: similar to the priority scheduler case. In `sema_down` and `sema_up`, a lock will be placed before an edit to the wait scheduler, with `lock_acquire` being before and `lock_release` afterwards. We also need to make analogous changes inside `cond_wait` and `cond_signal`.

3. Effective priority changes: `lock_acquire` must take place before the conditionals and `lock_release` will be issued after the setting is finished. This is to ensure that the donated priority is the effective priority of the donator **AT THE TIME OF THE DONATION**, instead of potentially some later alteration. For example, consider threads A, B, and C with priorities 3, 2, and 1, respectively. If B attempts to donate its priority to C, but was being blocked somehow, and during the blockage A donated its priority to B, B should **STILL** donate a priority of 2 to C, not 3 *(though eventually it would become 3 after A’s donation passes through)*.

Notice that the structs from above use lists, and as noted inside the design doc, lists are not thread-safe. However, any alterations to the lists must go through the structs, so changes to the lists will be gated off in a locked region, thus ensuring synchronization.

### Rationale:

Using the `priorityScheduler struct` guarantees constant run-time, since adding jobs to end of and removing jobs from the beginning of a linked list that keeps track of head/tail elements operates in constant time, and since array element access occurs in constant time. The `topPriority` variable also updates in constant time, because we check at most 63 lower priority levels to find the new highest priority in the case that we remove the last job from the previous highest priority. Bypassing the `topPriority` variable is not an efficient choice - if we did, we would have to traverse `allJobs` repeatedly every time we wanted to choose the next thread to run.

An alternative option might be to use a simple list of ready threads as is currently used. However, adding a thread with a certain priority in the correct position is much more time-consuming than using the `priorityScheduler struct`. Consider the case of `list_insertion` as defined by `list.h` and `list.c`. We supply the list element that we want to insert a new element right before - so, in the case that we had threads A, B, C, and D with priorities 40,60,50, and 50, inserted **IN THAT ORDER:**

**__Waitlist: B -> C -> D -> A__**

Now, we want to insert a thread, E, with priority 51. It would go right before C: we would call list_insert(C, E). To know where we would insert it, we would have to traverse potentially the whole list until we arrive at the last moment in which the following thread has lower priority than the thread we want to insert. This is a linear runtime insertion, and can substantially slow down our scheduling process. Consider an optimization: what if, for each priority, we kept a pointer to the “before-thread”, for example in the form of an array of threads, i.e. thread*. Whenever we add a thread, we would change all elements in the array corresponding to **HIGHER** priorities to obtain a before-thread value of the added thread, IF that added thread is the first thread at that particular priority level. Unfortunately, we would need additional resources to check whether it actually is the first thread added at that priority level, and the general logic behind such an array would be substantially more complicated than the priorityScheduler struct, because of the issue of propagation of a before-thread.

The reasoning for using a wait scheduler instead of a list of waiters for the synchronization primitives follows very similar lines of logic. As for why I use a separate wait structure instead of just a pointer to the priority scheduler itself, this is because a wait scheduler is **ONLY** dealing with threads waiting on a particular resource; the priority scheduler includes ALL ready threads; thus, it would be wasteful of space to have such a pointer to the priority scheduler, and there would need to be extra indicators as to which threads were waiting on what resource regardless.

The use of `effectivePriority` as the actual changing and referenced-for-comparison field inside the thread struct does not complicate scheduling - after all, it is just another int (which does not occupy much space). As for the list of donation recipients, if nested priority donation was not a case we had to handle, then it would not need to exist. However, because it does, there needs to be some way to go between indirect donation recipients: I reasoned that a recursive priority setting scheme would be simpler to implement code-wise than organizing pointers toward some common priority value, which was another option I considered, since it would require no recursion and instead only be changing a single value in space. However, keeping track of these pointers and manipulating them correctly would be much more complicated.

---

## Task 3: Multi-level Feedback Queue Scheduler (MLFQS)

### Data structures and functions

#### In `threads.h`:
```C
struct thread {
    ...
	fixed_point_t niceness; /* Niceness initialized to be zero in init_thread. */
	fixed_point_t recent_cpu;   /* Recent CPU initialized to be zero in init_thread. */
	...
}
```
#### In `threads.c`:
```C
fixed_point_t load_avg; /* load_avg initialized to be zero in thread_start. */
```
#### In `timer.c`:
```C
static  void
timer_interrupt (struct intr_frame *args UNUSED)
{
    ...
    if (thread_mlfqs)
    {
    	mlfqs_cur_thread_recent_cpu_add_one ();
    	if (ticks % TIMER_FREQ == 0)
        	mlfqs_update_load_avg();
        	mlfqs_update_all_thread_recent_cpu ();
    	if (ticks % 4 == 0)
        	mlfqs_update_all_thread_priority_then_sort ();
    }
    ...
}
/* thread_current()->recent_cpu add 1. */
void mlfqs_cur_thread_recent_cpu_add_one (void)
/* update the load avg */
void mlfqs_update_load_avg (void);
/* update all thread recent cpu */
void mlfqs_update_all_thread_recent_cpu (void);
/* update all thread priority and then sort the ready q according to priority */
void mlfqs_update_all_thread_priority_then_sort (void);
```
### Algorithms

#### Update `recent_cpu`, thread priority, and `load_avg`

We place the logic for updating `recent_cpu` , thread priority and `load_avg` in `thread_interrupt(...)` because this happens every tick.  The current running thread’s `recent_cpu` value will add one for every `thread_interrupt(...)`.  Then, if the `ticks` value is divisible by 4, we update every thread’s priority and sort the ready queue, and limit its min/max value to `PRI_MIN` or `PRI_MAX`, respectively.  If the `ticks` value is divisible by `TIMER_FREQ` we update all threads' `recent_cpu` and load_avg.

#### Scheduling the next thread

Because we sort the ready q every time will change a thread's priority, so the scheduler will always pick the thread with the highest priority. So If there are multiple threads with the highest priority, then the scheduler will cycle through each of these threads in ”round robin” fashion, because every time a thread is put on the ready queue, it is inserted according to it's priority, it will be placed at the end of the threads who has the same priority as him.

### Synchronization

All the update happen in `timer_interrupt(...)`, so we are in the middle of external interrupt. So I think we need to do nothing to prevent it from being interrupt.

### Rational

This implementation of MLFQS is straightforward and can satisfy all the requirements in the specification document. The majority of computation is done in the `timer_ticks`, which can be easily realized.

---

## Additional Questions:

### Q1: 

Consider a case where a thread A with lock A has just finished using the lock. Afterwards sema_up is called during lock_release, during which the next thread is selected to acquire lock A. Now, consider the case in which the next thread to acquire the lock *[according to queue ordering that solely inspects **BASE** priority]*, thread B, has BASE priority 20, while another thread, thread C, has BASE priority 19. 

However, consider the case that thread C possesses lock C, which thread D wants. Thread D has base priority 21; it does not wish to acquire lock A, but in order to acquire lock C as quickly as it can *(assuming lock C can only be released after thread C obtains lock A)*, it would normally donate its priority of 21 to thread C, so that thread C would gain effective priority 21, thus placing it at the front of the queue to be unblocked.

Without priority donation, this is impossible: thread D has no choice but to wait for thread B to finish using lock A, then give access to thread C, which finally allows thread D to acquire lock C. Suppose threads B and C print out their names while they hold lock A, and that thread D does not print out anything. Then, the output in the case of NO priority donation would be:

“My name is B”

“My name is C”

In the case that there **IS** priority donation, the threads would print out in order:

“My name is C”

“My name is B”

### Q2:

timer clicks | R(A) | R(B) | R( C) | P(A) | P(B) | P(C) | thread to run
------------|------|------|------|------|------|------|--------------
 0          | 0     | 0   | 0     | 63     |  61    | 59     |A
 4          |4      | 0     | 0     |  62    |  61    | 59     |A
 8          |8      | 0     | 0     |  61    |  61    | 59     |A
12          |12      | 0     | 0     | 60     | 61     | 59     |B
16          |12      | 4     | 0     | 60     | 60     | 59     |A
20          |16      |4      | 0     | 59     | 60     | 59     |B
24          | 16     |8      | 0     | 59     | 59     | 59     |A
28          |20      |8      |0      | 58     | 59     | 59     |B
32          | 20     |  12    |  0    |  58    |   58   |   59   |C
36          |   20   | 12     |    4  |    58  |    58  |   58   | A

### Q3:

Since the timer frequency  is not  specified in this question, we assumed that it is larger then 36 timer ticks, which means load average will never be updated in this process and they are initialized to 0. 

There is also ambiguity about which thread to select when we have multiple threads with the same priority.
In the example above, I just choose the next thread alphabetically, i.e. A, B, then C.

---

