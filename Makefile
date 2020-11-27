# .DEFAULT_GOAL=all

sut: sut.c
	gcc -o sut.o -c sut.c
	gcc -o a1_lib.o -c a1_lib.c
