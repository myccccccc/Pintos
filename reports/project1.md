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

Jingqi Wang:
I am responsible for the original design document of task 3 with Yingchun Ma. Afterwards, I implemented the entire first version of task 3 and made several modifications to the original design, like using different rule to update priority. However, there are some bugs among the first version of code which are difficult to find. So the final implementation are from Jiasheng Qin. Besides, I also helped to solve problems encountered by other teammates.