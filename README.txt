COMPILING INFO
I used both the queue.h and a1_lib.h in my sut.c file.

In terminal, run "make sut"
In order to test library code with test program test.c, run "gcc test.c a1_lib.o sut.o -lpthread"
Finally, run "./a.out"


SERVER INFO
I tested my IO functions by tweaking my backend.c file from assignment1. 
I did not use the provided shell because I was having trouble getting everything to work correctly.
I compiled my bacend tester with the comannd "gcc backend_tester.c a1_lib.c"
