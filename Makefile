prefix ?= /usr/local
bindir ?= $(prefix)/bin
DESTDIR ?=
INSTALL ?= install

.PHONY: all clean test test-nextufs test-fsck fsck nextufs install uninstall

all: nextufs

nextufs:
	$(MAKE) -C nextufs -f Makefile.linux

fsck:
	$(MAKE) -C nextufs.fsck -f Makefile.linux

fsck: nextufs

test: test-nextufs test-fsck

test-nextufs:
	$(MAKE) -C nextufs -f Makefile.linux test

test-fsck:
	$(MAKE) -C nextufs.fsck -f Makefile.linux repair-smoke

test-fsck: test-nextufs

clean:
	$(MAKE) -C nextufs -f Makefile.linux clean
	$(MAKE) -C nextufs.fsck -f Makefile.linux clean

install: nextufs
	$(INSTALL) -d "$(DESTDIR)$(bindir)"
	$(INSTALL) -m 0755 nextufs/nextufs "$(DESTDIR)$(bindir)/nextufs"

uninstall:
	rm -f "$(DESTDIR)$(bindir)/nextufs"
	rm -f "$(DESTDIR)$(bindir)/nextufs.fsck"
