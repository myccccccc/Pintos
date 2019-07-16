Final Report for Project 1: Threads
===================================

Changes (from design doc):

Task 1:

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

Personal contributions:

Jiasheng Qin:
I created a version of task 2 conforming to the original design doc implementation. However, it encountered a 
variety of synchronization issues, at which point we decided to switch to another implementation. After the 
switch, I created and tested the entirety of the current implementation of task 3 (the MLFQS-related functions 
inside thread.c and handling the variables inside thread.h). Potential improvements include better communication 
between group members regarding bugs that popped up and also discrepancies between implementations. Scheduling 
our workload was fairly effective, and we managed to finish the code for this project a day early.
