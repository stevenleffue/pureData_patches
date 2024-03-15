NAME=text
CSYM=$(NAME)

include ../makefile.include

text.l_ia64: text.c file.c
	$(CC) $(LINUXCFLAGS) $(LINUXINCLUDE) -fPIC -c text.c
	$(CC) $(LINUXCFLAGS) $(LINUXINCLUDE) -fPIC -c file.c
	ld -shared -o $*.l_ia64 text.o file.o -lc -lm
	strip --strip-unneeded $*.l_ia64
	rm $*.o
