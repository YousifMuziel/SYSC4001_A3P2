# SYSC4001 – Assignment 3 – Part 2  
Student: 101231344

## Files in this repository

- `part2a_101231344.c`  
  Concurrent TA simulation using **shared memory only**.  
  No synchronisation – race conditions are possible and expected.

- `part2b_101231344.c`  
  Same simulation, but using **semaphores + shared memory** to
  synchronise all critical sections.

- `rubric.txt`  
  Rubric file with 5 lines, one per question:

  ```text
  1, A
  2, B
  3, C
  4, D
  5, E
  
- `exam_files`

exam_0001.txt … exam_0020.txt
At least 20 exam files. Each file contains one line with a
4-digit student number:

text
Copy code
0001
The exam file whose number is 9999 (here exam_0020.txt) is used to
terminate the simulation.

text
Copy code
exam_0001.txt
exam_0002.txt
...
exam_0020.txt
reportPartC.pdf


How to compile
On a Linux machine with gcc:

bash

gcc -Wall -Wextra -std=c11 -o part2a_101231344 part2a_101231344.c
gcc -Wall -Wextra -std=c11 -pthread -o part2b_101231344 part2b_101231344.c

(-pthread pulls in the implementation for POSIX semaphores.)

How to run

Make sure rubric.txt, all the exam_*.txt files and exam_list.txt
are in the same directory as the executables.

Run Part 2(a) (race-condition version) with, for example, 3 TAs:

bash

./part2a_101231344 3 rubric.txt exam_list.txt
The processes print what they are doing:

reviewing the rubric,

correcting a question’s rubric,

marking questions for each exam,

loading the next exam.

Because there is no synchronisation, the same question can be marked
by multiple TAs and rubric corrections can overlap.

Run Part 2(b) (semaphore version):

bash

./part2b_101231344 3 rubric.txt exam_list.txt
Here:

rubric corrections are protected by mutex_rubric,

question selection and exam loading are protected by mutex_exam,

printing is serialised by mutex_print.

Each question is marked exactly once for each exam, and exams are
processed sequentially until the exam with student number 9999
is completed.

Design in the context of the critical-section requirements
The shared data that must be protected in Part 2(b) are:

Rubric (rubric[]) – all TAs read the rubric, and occasionally
one TA decides to modify it.

In Part 2(b) a semaphore mutex_rubric ensures only one TA can
modify any rubric line at a time.

Current exam and questions:

current_exam_index, current_student_id

question_marked[5]

These indicate which exam is currently in shared memory and which
questions have already been taken by some TA.

In Part 2(b) all updates to these fields are performed while holding
the semaphore mutex_exam. This prevents two TAs from taking the
same question or loading a new exam at the same time.

Output printing:
Printing is not logically critical for correctness, but without
synchronisation the messages become unreadable. 

The three classical requirements for a correct critical-section solution
are:

Mutual exclusion
Only one process at a time is executing inside a critical section.
In my solution, mutual exclusion is guaranteed by the semaphores:

any rubric update occurs inside sem_wait(&mutex_rubric) /
sem_post(&mutex_rubric);

question selection and loading the next exam occur inside
sem_wait(&mutex_exam) / sem_post(&mutex_exam).

Progress
If no process is executing in a critical section and some processes
wish to enter it, the choice of which process enters next cannot be
postponed indefinitely.

The OS implementation of semaphores ensures that a process blocked in
sem_wait will enter the critical section once another process calls
sem_post. There are no busy-wait loops; TAs either work or block.

Bounded waiting

There exists a limit on the number of times other processes are allowed
to enter their critical sections after a process has requested entry and
before the requesting process is granted access.

In this solution, every sem_wait eventually succeeds as long as other
processes execute and call sem_post in finite time. No TA holds a
semaphore while waiting for another semaphore, and critical sections are
short, so starvation is not expected in practice.

Part 2(a) intentionally violates these requirements (there is no mutual
exclusion). It demonstrates non-deterministic behaviour and race
conditions: rubric lines can be changed multiple times in conflicting
ways, and multiple TAs can mark the same question. Part 2(b) adds
semaphores on top of the same shared memory layout and satisfies the
three requirements above, producing a correct and repeatable behaviour.
