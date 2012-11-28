test: hp2.o test.c test_data.h
	gcc test.c hp2.o -o test
	./test

hp2.o: hp2.c hp2.h
	gcc hp2.c -Wall -pedantic-errors -std=c89 -c -o hp2.o

clean:
	rm -f hp2.o test
