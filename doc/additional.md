
Design Document for Project 1: Threads
======================================

## Group Members
* Jiasheng Qin <jqin0713@berkeley.edu> 
* Jingqi Wang <jackiewang@berkeley.edu>
* Yingchun Ma <mayingchun@berkeley.edu> 
* Zewei DIng <ding.zew@berkeley.edu>

--
## Additional Questions:

1. 

2. 
icks | R(A) | R(B) | R( C) | P(A) | P(B) | P(C) | thread to run
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

3.
Since the timer frequency  is not  specified in this question, we assumed that it is larger then 36 timer ticks, which means load average will never be updated in this process and they are initialized to 0. 

There is also ambiguity about which thread to select when we have multiple threads with the same priority

In the example above, I just choose the next thread alphabetically, i.e. A, B, then C.
