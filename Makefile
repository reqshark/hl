run-tests: hp2.o test.c test_data.h
	clang test.c hp2.o -g -o run-tests

hp2.o: hp2.c hp2.h
	clang hp2.c -g -Wall -pedantic-errors -std=c89 -c -o hp2.o

clean:
	rm -f hp2.o run-tests
