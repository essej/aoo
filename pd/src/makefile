# makefile for pd externals used for AoO
#
current: pd_linux

clean: ; rm -f *.pd_linux *.o

PDSOURCE = /usr/local/include

SOURCES=$(sort $(filter %.c, $(wildcard *.c)))

LINUXOBJECTS = $(SOURCES:.c=.pd_linux)

pd_linux: $(LINUXOBJECTS)

.SUFFIXES: .pd_linux

LINUXCFLAGS = -DPD -O2 \
    -funroll-loops -fomit-frame-pointer -fPIC -fno-strict-aliasing \
    -fPIC -Wall -W -Wshadow -Wstrict-prototypes \
    -Wno-unused -Wno-parentheses -Wno-switch $(CFLAGS)

LINUXINCLUDE =  -I$(PDSOURCE)

.c.pd_linux:
	$(CC) $(LINUXCFLAGS) $(LINUXINCLUDE) -o $*.o -c $*.c
	$(CC) -export_dynamic -shared -o $*.pd_linux $*.o -lc -lm
	strip --strip-unneeded $*.pd_linux
	rm -f $*.o

