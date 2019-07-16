
Final Report for Project 1: Threads

===================================

  

Changes (from design doc):

  

Task 1 (from Dean's perspective):

Due to this is the first task, we don’t change anything from design doc. However, I still would like to reiterate the process with more details to make sure that I fully understand the rationale of multi-thread design in alarm.

  

From project spec on Task 1, it mentions that we need to re-implement the function timer_sleep() under timer.c to make the alarm execute without “busy waiting”. However, the ticks, which works like clock, would help us to record timestamp info.

  

In the first place, I would like to work with the timer_sleep() function. Because this function will block the current thread, we need to record something to help us know what’s the current time when I block this thread, and when the thread will be waked up. Because the TIMER_FREQ timer is already implemented and work as system clock, we’ve already have the current ticks which is the ticks in the timer.c method at global state. Thus, I only need to tell the function how long the current thread will be blocked, namely taking sleeping_ticks as argument and pass into the function. Also, we don’t need have return value because we just block the current thread.

  

So, to implement this function, I do the sanity check for timer_sleep() to make sure the ticks is positive. If the ticks is negative, it is meaningless. On the next step, I need to turn off the interrupt with intr_disable() to make sure that during the time I create and block current thread, no other thread preemption happens during this period, and I also get the return value of this function, which is the previous interrupt state. Based on the current ticks, I create a new thread with function thread_current(). Then, I set the thread ticks_wakeup field with input value to tell the thread when to stop the blocked state. Next, add the thread to sleeping thread list with adding_thread_sleeping_list() function and use thread_block() to block the thread I just created. At the last, I need to resume the interrupt the state with intr_set_level() with previous return value.

  

Meantime, we need to implement the adding_thread_sleeping_list() function to help us maintain a sleeping_list with blocked threads. Still, we need to first turn off the interrupt state. Because the sleeping_list is empty since it is initialized, we could use list_insert_ordered() to make the list ordered as we insert new element. Finally, we need to reset the intr_level. The comparator of the list element is implemented by wakeup_cmp_priority() and make sure the order of the sleeping_list is ascending with wakeup_ticks value.

  

Furthermore, I need to implement the wakeup_sleeping_thread () to unblock the sleeping thread. Again, this function need to take system ticks as parameter and doesn’t need the return value, because the sleeping_list is global variable. In the first place, we need to do sanity check on the sleeping_list with list_empty(). If the list is empty, we just need to return the function. Then, we need to turn off the interrupt state with intr_disable() as what we do during we block the thread. Next, we iterate the sleeping_list and get current thread t. If the current thread t’s ticks_wakeup <= ticks, this means the sleeping time for t is over, we could wakeup the thread with thread_unblock(). Meantime, we also need to remove the current thread t out of the sleeping_list. Because the sleeping_list is sorted as we create it, once we find the current thread t’s ticks_wakeup >= ticks, we break the iteration. Finally, we need to reset the intr_level.

  

However, how could we call the wakeup_sleeping_thread() function to wake up the thread? Since timer_interrupt() happen at every tick. Different from the design doc, we change the thread iteration from this function to wakeup_sleeping_thread(), which make the function with more semantics. Thus, on each tick, we wakeup sleeping list by calling timer_interrupt().

  

Finally, we need to do some miscellaneous work. We need to declare and initialize the spleeping_list at the head of thread.c.

  

I think my teammates are doing the project really well and they spend a lot of time to develop and polish the code. I do the report part on task 1. Yinchun does the design doc of task 1 and task 2, and implementation of task 1 and task 2. Jiasheng does the design doc of task 2 and additional questions, and implementation of task 2. Jingqi does the design doc of task 3 and additional questions, and implementation of task 3

  
  

Task 2:

On the original design doc, we choose to use 64 slot array corresponding to the 54 priority values.   Create two scheduler struct, priorityScheduler and waitScheduler, to keep track of priority threads and waiting threads.   And also create donationRecipients to keep track of threads which receive donations.   However, adding these classes will make the implementation really complicated. Jiasheng was originally working on task2, but it seems that his design from the design doc encountered some bugs, so I made some new designs about task2 and passed all the tast cases.
#### In `threads.h`:
```C
struct thread {
    ...
    /* list of locks that the thread is currently holding */
	struct  list  locks_holding;
	/* the lock that the thread is currently waiting for */
	struct  lock  *lock_waiting;
    ...
}
```
#### In `synch.c`:
```c
/* Lock. */

struct  lock
{
	...
	struct  list threads_waiting; /* threads that are currently waiting for this lock */
	int max_priority; /* max priority among threads_waiting*/
	...
};
```
In `lock_acquire (struct lock *lock)`, before doing `sema_down (&lock->semaphore)`, we need to do priority donation
```c
old_level =  intr_disable();
if (!thread_mlfqs)
{
	thread_current()->lock_waiting  = lock;
	list_push_back(&lock->threads_waiting, &thread_current()->loc_elem);
	if (thread_current()->priority  > lock->max_priority)
	{
		lock->max_priority  =  thread_current()->priority;
	}
	if (lock->holder  !=  NULL)
	{
		if (thread_current()->priority  > lock->holder->priority)
		{
			priority_donation(thread_current(), lock->holder);
		}
	}
}
intr_set_level(old_level);
```
In `priority_donation(thread_current(), lock->holder)` I'm basically doing recursive calls to change a thread priority, also if this thread is in the ready list, because the thread's priority is changeing, so we have to insert this thread into the ready list again to keep the ready list sorted according to its priority
```c
void  priority_donation(struct thread *a, struct thread *b)
{
	ASSERT(a->priority  > b->priority);
	b->priority  = a->priority;
	if (b->lock_waiting  !=  NULL)
	{
		ASSERT(b->status  != THREAD_READY);
		if (b->priority  > b->lock_waiting->max_priority)
		{
			b->lock_waiting->max_priority  = b->priority;
		}
		if (b->lock_waiting->holder  !=  NULL)
		{
			if (b->priority  > b->lock_waiting->holder->priority)
			{
				priority_donation(b, b->lock_waiting->holder);
			}
		}
	}
	else  if (b->status  == THREAD_READY)
	{
		list_remove (&b->elem);
		list_insert_ordered (&ready_list, &b->elem, (list_less_func *) 			thread_cmp_priority, NULL);
	}
}
```
After `sema_down(&lock->semaphore)`:
```c
old_level =  intr_disable();
if (!thread_mlfqs)
{
	thread_current()->lock_waiting  =  NULL;
	list_push_back(&thread_current()->locks_holding, &lock->loc_elem);
	list_remove(&thread_current()->loc_elem);
	lock->max_priority  =  lock_get_max_priority(lock);
}
intr_set_level(old_level);
```
`lock_get_max_priority(lock)` will look through `threads_waiting` list to get the highest priority among all threads that is currently waiting for that lock.

In `lock_release (struct lock *lock)` before `sema_up (&lock->semaphore)` we need to change some threads priority, because the thread which is releasing the lock may be donated by this lock. So after releasing, this thread will not be donated by this lock. It will be another resursive implemention.
```c
void
lock_release (struct lock *lock)
{
...
old_level =  intr_disable();
if (!thread_mlfqs)
{
	pre_priority =  thread_current()->priority;
	list_remove(&lock->loc_elem);
	if (thread_current()->priority  == lock->max_priority) //this thread was donated by this lock he is releasing
	{
	thread_current()->priority  =  		get_priority_among_locks_holding(thread_current());
	ASSERT(thread_current()->priority  <= pre_priority);
		if (thread_current()->lock_waiting  !=  NULL)//this thread may be donating another thread
		{
		ASSERT(thread_current()->lock_waiting->max_priority  >= pre_priority);
			if (thread_current()->priority  < pre_priority &&  thread_current()->lock_waiting->max_priority  == pre_priority)//the lock this thread is currently waiting has it's max_priority because of this thread
			{
				thread_priority_resume(thread_current());
			}
		}
	}
}
intr_set_level(old_level);
sema_up (&lock->semaphore);
}

/* thread t priority just decreases, he may be now donating other thread, this function will fix this, it is recursive */
void  thread_priority_resume(struct thread *t);
```
Also in `sema_up (struct semaphore *sema)` at the end we add `thread_yield()` because after the last step of `lock_release(...)` the thread's priority may change, so we need to reschedule.

At last, we always keeps `ready_list`, and `waiter` lists in `struct  semaphore` and `struct  condition` sorted. Every time we add new threads in these lists we sorted by threads' priority. And when we chage the thread's priority in these lists we resorted them.

Task 3:

1) Nice variable inside thread.h was changed to be an int (from a fixed_point_t); although calculations involving

it would need an extra call to fix_int, keeping nice as an int adhered more strong to the semantics of the nice value

specified in the project manual.

2) Load_avg was moved from threads.h to threads.c. There was no functional difference in doing so (for the purpose

of our implementation of the project), but it seemed more aesthetically correct to have all the MLFQS variables

declared inside one file.

3) Update functions (for load_average, recent_cpu, and priority) were all moved to thread.c (originally

they were planned to be in timer.c). The reason was to lessen how far we split up code; given that thread_tick

(in thread.c) is called every time timer_interrupt (in timer.c) is called, we found that there was no reason to

include the methods in the latter; in addition, it was easier to debug issues since we could cross-reference

existing functions inside thread.c with more ease, instead of having to look back and forth between files.

4) There were changes made to synch.c for MLFQS handling; specifically, priority donation aspects related

to locks (in lock_acquire and lock_release) were gated off by a Boolean check to thread_mlfqs – if the Boolean

was true, then priority donation would not occur. [NOTE: this isn’t really a change to the design doc – it was

just something not mentioned there.]

5) A few extra helper functions were added, namely ones that updated a single thread (specified in the method parameters)

in terms of priority and recent_cpu; the methods that updated all threads would then call thread_foreach by passing in

these single-thread functions.

6) Thread_set_priority is gated off by a check to thread_mlfqs: if the Boolean is true, then we return immediately,

essentially ignoring all calls to it. [NOTE: this isn’t really a change to the design doc – it was just something

not mentioned there.]

7) Thread_create was altered in the case of MLFQS scheduling: it would ignore the priority argument and instead

set priority based on the parent thread (or be 0 if it was the main thread). [NOTE: this isn’t really a change

to the design doc – it was just something not mentioned there.]

  

------------------------------------------------

  

Personal contributions (additional notes):

  
  

Jiasheng Qin:

I created a version of task 2 conforming to the original design doc implementation. However, it encountered a

variety of synchronization issues, at which point we decided to switch to another implementation. After the

switch, I created and tested the entirety of the current implementation of task 3 (the MLFQS-related functions

inside thread.c and handling the variables inside thread.h). Potential improvements include better communication

between group members regarding bugs that popped up and also discrepancies between implementations. Scheduling

our workload was fairly effective, and we managed to finish the code for this project a day early.