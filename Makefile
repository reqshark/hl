tests: hl.o tests.c test_data.h
	clang tests.c hl.o -g -o tests

hl.o: hl.c hl.h
	clang hl.c -g -Wall -pedantic-errors -std=c89 -c -o hl.o

tags: hl.h hl.c tests.c test_data.h
	ctags $^

clean:
	rm -f hl.o tests tags

.PHONY: clean
