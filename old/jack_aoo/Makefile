#
# AoO - audio over osc jack client
# 
# Copyright (c) 2014 Winfried Ritsch <ritsch_at_algo.mur.at>
# 
# This library is covered by the LGPL, read licences
# at <http://www.gnu.org/licenses/>  for details
# 
#

# --- edit down here ---
#
PREFIX = /usr/local
SUFFIX := $(shell uname -m | sed -e 's/^unknown/$//' -e 's/^i.86/$//' -e 's/^x86_64/$/64/')
LIBDIR = lib$(SUFFIX)
DISTDIR = aoo_jack-$(VERSION)
VERSION = 0.1.0
MANDIR = /usr/share/man/man1
LDFLAGS += -L$(PREFIX)/$(LIBDIR)
CFLAGS += -MMD -MP -DVERSION=\"$(VERSION)\"
CFLAGS += -O3  -ffast-math -Wall
CFLAGS += -march=native

all:	aoo_jack

AOO_JACK_O = aoo_jack.o
aoo_jack:	LDLIBS += -laoo -lrt
aoo_jack:	$(AOO_JACK_O)
	gcc $(LDFLAGS) -o $@ $(AOO_JACK_O) $(LDLIBS)
$(AOO_JACK_O):
-include $(AOO_JACK_O:%.o=%.d)

aoo_jack.1.gz:	aoo_jack.1
	gzip -c aoo_jack.1 > aoo_jack.1.gz

install:	all
	install -Dm 755 aoo_jack $(DESTDIR)$(PREFIX)/bin/aoo_jack
	install -Dm 644 aoo_jack.1.gz $(DESTDIR)$(MANDIR)/aoo_jack.1.gz

uninstall: 
	/bin/rm -f $(DESTDIR)$(PREFIX)/bin/aoo_jack
	/bin/rm -f $(MANDIR)/aoo_jack.1.gz

clean:
	/bin/rm -f *~ *.o *.a *.d *.so *.gz aoo_jack
