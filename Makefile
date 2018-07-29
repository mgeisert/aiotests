all::	example.exe heapxfer.exe sigtest.exe

example.exe:	example.c
	gcc -Wall -g -o example example.c

heapxfer.exe:	heapxfer.c
	gcc -Wall -g -o heapxfer heapxfer.c

sigtest.exe:	sigtest.c
	gcc -Wall -g -o sigtest sigtest.c

clean:
	rm -f example.exe heapxfer.exe sigtest.exe
