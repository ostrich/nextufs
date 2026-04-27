prefix ?= /usr/local
bindir ?= $(prefix)/bin
DESTDIR ?=
INSTALL ?= install

.PHONY: all clean test test-nextufs test-fsck test-mkimg test-resize fsck mkimg resize nextufs install uninstall

all: nextufs fsck mkimg resize

nextufs:
	$(MAKE) -C nextufs -f Makefile.linux

fsck:
	$(MAKE) -C nextufs.fsck -f Makefile.linux

mkimg:
	$(MAKE) -C nextufs.mkimg -f Makefile.linux

resize:
	$(MAKE) -C nextufs.resize -f Makefile.linux

test: test-nextufs test-fsck test-mkimg test-resize

test-nextufs:
	$(MAKE) -C nextufs -f Makefile.linux test

test-fsck:
	$(MAKE) -C nextufs.fsck -f Makefile.linux repair-smoke

test-mkimg:
	$(MAKE) -C nextufs.mkimg -f Makefile.linux test

test-resize:
	$(MAKE) -C nextufs.resize -f Makefile.linux test

clean:
	$(MAKE) -C nextufs -f Makefile.linux clean
	$(MAKE) -C nextufs.fsck -f Makefile.linux clean
	$(MAKE) -C nextufs.mkimg -f Makefile.linux clean
	$(MAKE) -C nextufs.resize -f Makefile.linux clean

install: all
	$(INSTALL) -d "$(DESTDIR)$(bindir)"
	$(INSTALL) -m 0755 nextufs/nextufs "$(DESTDIR)$(bindir)/nextufs"
	$(INSTALL) -m 0755 nextufs/nextufs_probe "$(DESTDIR)$(bindir)/nextufs_probe"
	$(INSTALL) -m 0755 nextufs.fsck/nextufs.fsck "$(DESTDIR)$(bindir)/nextufs.fsck"
	$(INSTALL) -m 0755 nextufs.mkimg/nextufs.mkimg "$(DESTDIR)$(bindir)/nextufs.mkimg"
	$(INSTALL) -m 0755 nextufs.resize/nextufs.resize "$(DESTDIR)$(bindir)/nextufs.resize"

uninstall:
	rm -f "$(DESTDIR)$(bindir)/nextufs"
	rm -f "$(DESTDIR)$(bindir)/nextufs_probe"
	rm -f "$(DESTDIR)$(bindir)/nextufs.fsck"
	rm -f "$(DESTDIR)$(bindir)/nextufs.mkimg"
	rm -f "$(DESTDIR)$(bindir)/nextufs.resize"
