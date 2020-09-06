# comp3231
this repository includes 4 assignments from comp3231 Operating System at University of New South Wales 20T1

# Assignemt 00
- Warm up

The aim of the warmup exercise is to have you familiarise yourself with the environment that you will be using for the more substantial assignments. The exercise consists of two parts: an assessed exercise for marks, and a non-assessable component. The non-assessable component consists of a set of directed questions to give you practice navigating and reading the code base. The answers to this code reading Q&A component of the exercise are available on the wiki (improvable by you the students). The assessable exercise consists of you: learning how to build and run OS/161, making a very minor change to the existing OS to fix a bug, and learning the submission process. The change is conceptually trivial, so you can view this exercise as us giving away marks as an incentive for you to get the assignment environment up and running early in the trimester in preparation for the assignments.



# Assignment 01
- Synchronisation

In this assignment you will solve a number of synchronisation problems within the software environment of the OS/161 kernel. By the end of this assignment you will gain the skills required to write concurrent code within the OS/161 kernel, though the synchronisation problems themselves are only indirectly related to the services that OS/161 provides.



# Assignment 02
- System calls and processes

In this assignment you will be implementing a software bridge between a set of file-related system calls inside the OS/161 kernel and their implementation within the VFS (obviously also inside the kernel). Upon completion, your operating system will be able to run a single application at user-level and perform some basic file I/O.
A substantial part of this assignment is understanding how OS/161 works and determining what code is required to implement the required functionality. Expect to spend at least as long browsing and digesting OS/161 code as actually writing and debugging your own code.



# Assignment 03
- Virtual memory

In this assignment you will implement the virtual memory sub-system of OS/161. The existing VM implementation in OS/161, dumbvm, is a minimal implementation with a number of shortcomings. In this assignment you will adapt OS/161 to take full advantage of the simulated hardware by implementing management of the MIPS software-managed Translation Lookaside Buffer (TLB). You will write the code to manage this TLB.
