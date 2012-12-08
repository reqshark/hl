tests: hp2.o tests.c test_data.h
	clang tests.c hp2.o -g -o tests

hp2.o: hp2.c hp2.h
	clang hp2.c -g -Wall -pedantic-errors -std=c89 -c -o hp2.o

tags: hp2.h hp2.c tests.c test_data.h
	ctags $^

clean:
	rm -f hp2.o tests tags

.PHONY: clean
