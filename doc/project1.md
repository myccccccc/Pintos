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


