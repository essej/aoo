#
# AoO - audio over osc library 
# 
# Copyright (c) 2014 Winfried Ritsch <ritsch_at_algo.mur.at>
# 
# This library is covered by the GPL, read licences
# at <http://www.gnu.org/licenses/>  for details
# 
#

# --- edit down here ---
#
PREFIX = /usr/local
SUFFIX := $(shell uname -m | sed -e 's/^unknown/$//' -e 's/^i.86/$//' -e 's/^x86_64/$/64/')
LIBDIR = lib$(SUFFIX)

MAJVERS = 0
MINVERS = 1.0
VERSION = $(MAJVERS).$(MINVERS)
DISTDIR = aoo-$(VERSION)

CFLAGS += -I. -D_REENTRANT -D_POSIX_PTHREAD_SEMANTICS
CFLAGS += -Wall -fPIC -O2 -ffast-math
CFLAGS += -march=native
LDFLAGS += 
LDLIBS +=


AOO_SO = libaoo.so
AOO_MAJ = $(AOO_SO).$(MAJVERS)
AOO_MIN = $(AOO_MAJ).$(MINVERS)
AOO_DEP =
AOO_O = aoo.o
AOO_H = aoo/aoo.h


$(AOO_MIN): $(AOO_O)
	gcc -shared $(LDFLAGS) -Wl,-soname,$(AOO_MAJ) -o $(AOO_MIN) $(AOO_O) $(AOO_DEP)

$(AOO_O):	$(AOO_H)

install:	$(AOO_MIN)
	install -d $(DESTDIR)$(PREFIX)/include/aoo
	install -d $(DESTDIR)$(PREFIX)/$(LIBDIR)
	install -m 644 $(AOO_H) $(DESTDIR)$(PREFIX)/include/aoo
	install -m 644 $(AOO_MIN) $(DESTDIR)$(PREFIX)/$(LIBDIR)
	ln -sf $(AOO_MIN) $(DESTDIR)$(PREFIX)/$(LIBDIR)/$(AOO_SO)
	ldconfig

uninstall:
	/bin/rm -rf $(DESTDIR)$(PREFIX)/include/aoo
	/bin/rm -rf $(DESTDIR)$(PREFIX)/$(LIBDIR)/libaoo*

clean:
	/bin/rm -rf *~ *.o *.a *.d *.so.* aoo/*~
