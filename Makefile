run-tests: hp2.o test.c test_data.h
	gcc test.c hp2.o -g -o run-tests

hp2.o: hp2.c hp2.h
	gcc hp2.c -g -Wall -pedantic-errors -std=c89 -c -o hp2.o

clean:
	rm -f hp2.o run-tests
