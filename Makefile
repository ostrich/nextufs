prefix ?= /usr/local
bindir ?= $(prefix)/bin
DESTDIR ?=
INSTALL ?= install

.PHONY: all clean test test-nextufs test-fsck test-mkimg fsck mkimg nextufs install uninstall

all: nextufs fsck mkimg

nextufs:
	$(MAKE) -C nextufs -f Makefile.linux

fsck:
	$(MAKE) -C fsck.nextufs -f Makefile.linux

mkimg:
	$(MAKE) -C nextufs.mkimg -f Makefile.linux

test: test-nextufs test-fsck test-mkimg

test-nextufs:
	$(MAKE) -C nextufs -f Makefile.linux test

test-fsck:
	$(MAKE) -C fsck.nextufs -f Makefile.linux repair-smoke

test-mkimg:
	$(MAKE) -C nextufs.mkimg -f Makefile.linux test

clean:
	$(MAKE) -C nextufs -f Makefile.linux clean
	$(MAKE) -C fsck.nextufs -f Makefile.linux clean
	$(MAKE) -C nextufs.mkimg -f Makefile.linux clean

install: all
	$(INSTALL) -d "$(DESTDIR)$(bindir)"
	$(INSTALL) -m 0755 nextufs/nextufs "$(DESTDIR)$(bindir)/nextufs"
	$(INSTALL) -m 0755 nextufs/nextufs_probe "$(DESTDIR)$(bindir)/nextufs_probe"
	$(INSTALL) -m 0755 fsck.nextufs/fsck.nextufs "$(DESTDIR)$(bindir)/fsck.nextufs"
	$(INSTALL) -m 0755 nextufs.mkimg/nextufs.mkimg "$(DESTDIR)$(bindir)/nextufs.mkimg"

uninstall:
	rm -f "$(DESTDIR)$(bindir)/nextufs"
	rm -f "$(DESTDIR)$(bindir)/nextufs_probe"
	rm -f "$(DESTDIR)$(bindir)/fsck.nextufs"
	rm -f "$(DESTDIR)$(bindir)/nextufs.mkimg"
