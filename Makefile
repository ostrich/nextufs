prefix ?= /usr/local
bindir ?= $(prefix)/bin
DESTDIR ?=
INSTALL ?= install

.PHONY: all clean test test-write test-write-big test-write-grow test-unlink test-mkdir test-rewrite test-link-symlink test-rmdir test-meta test-rename test-truncate test-special test-fuse-write test-permissions test-failure fsck mkfs nextufs install uninstall

all: nextufs fsck mkfs

nextufs:
	$(MAKE) -C nextufs -f Makefile.linux

fsck:
	$(MAKE) -C fsck.nextufs -f Makefile.linux

mkfs:
	$(MAKE) -C mkfs.nextufs -f Makefile.linux

test:
	$(MAKE) -C nextufs -f Makefile.linux test

test-write:
	$(MAKE) -C nextufs -f Makefile.linux test-write

test-write-big:
	$(MAKE) -C nextufs -f Makefile.linux test-write-big

test-write-grow:
	$(MAKE) -C nextufs -f Makefile.linux test-write-grow

test-unlink:
	$(MAKE) -C nextufs -f Makefile.linux test-unlink

test-mkdir:
	$(MAKE) -C nextufs -f Makefile.linux test-mkdir

test-rewrite:
	$(MAKE) -C nextufs -f Makefile.linux test-rewrite

test-link-symlink:
	$(MAKE) -C nextufs -f Makefile.linux test-link-symlink

test-rmdir:
	$(MAKE) -C nextufs -f Makefile.linux test-rmdir

test-meta:
	$(MAKE) -C nextufs -f Makefile.linux test-meta

test-rename:
	$(MAKE) -C nextufs -f Makefile.linux test-rename

test-truncate:
	$(MAKE) -C nextufs -f Makefile.linux test-truncate

test-special:
	$(MAKE) -C nextufs -f Makefile.linux test-special

test-fuse-write:
	$(MAKE) -C nextufs -f Makefile.linux test-fuse-write

test-permissions:
	$(MAKE) -C nextufs -f Makefile.linux test-permissions

test-failure:
	$(MAKE) -C nextufs -f Makefile.linux test-failure

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
