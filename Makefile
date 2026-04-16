prefix ?= /usr/local
bindir ?= $(prefix)/bin
DESTDIR ?=
INSTALL ?= install

.PHONY: all clean test test-nextufs test-fsck test-mkfs fsck mkfs nextufs install uninstall

all: nextufs fsck mkfs

nextufs:
	$(MAKE) -C nextufs -f Makefile.linux

fsck:
	$(MAKE) -C fsck.nextufs -f Makefile.linux

mkfs:
	$(MAKE) -C mkfs.nextufs -f Makefile.linux

test:
	$(MAKE) -C nextufs -f Makefile.linux test

test-nextufs:
	$(MAKE) -C nextufs -f Makefile.linux test

test-fsck:
	$(MAKE) -C fsck.nextufs -f Makefile.linux repair-smoke

test-mkfs:
	$(MAKE) -C mkfs.nextufs -f Makefile.linux test-reproducible

clean:
	$(MAKE) -C nextufs -f Makefile.linux clean
	$(MAKE) -C fsck.nextufs -f Makefile.linux clean
	$(MAKE) -C mkfs.nextufs -f Makefile.linux clean

install: all
	$(INSTALL) -d "$(DESTDIR)$(bindir)"
	$(INSTALL) -m 0755 nextufs/nextufs "$(DESTDIR)$(bindir)/nextufs"
	$(INSTALL) -m 0755 nextufs/nextufs_probe "$(DESTDIR)$(bindir)/nextufs_probe"
	$(INSTALL) -m 0755 fsck.nextufs/fsck.nextufs "$(DESTDIR)$(bindir)/fsck.nextufs"
	$(INSTALL) -m 0755 mkfs.nextufs/mkfs.nextufs "$(DESTDIR)$(bindir)/mkfs.nextufs"

uninstall:
	rm -f "$(DESTDIR)$(bindir)/nextufs"
	rm -f "$(DESTDIR)$(bindir)/nextufs_probe"
	rm -f "$(DESTDIR)$(bindir)/fsck.nextufs"
	rm -f "$(DESTDIR)$(bindir)/mkfs.nextufs"
