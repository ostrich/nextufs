prefix ?= /usr/local
bindir ?= $(prefix)/bin
DESTDIR ?=
INSTALL ?= install

.PHONY: all clean test test-nextufs test-fsck test-mkimg test-resize fsck mkimg resize nextufs install uninstall

all: nextufs

nextufs:
	$(MAKE) -C nextufs -f Makefile.linux

fsck:
	$(MAKE) -C nextufs.fsck -f Makefile.linux

mkimg:
	$(MAKE) -C nextufs.mkimg -f Makefile.linux

resize:
	$(MAKE) -C nextufs.resize -f Makefile.linux

fsck resize: nextufs

test: test-nextufs test-fsck test-mkimg test-resize

test-nextufs:
	$(MAKE) -C nextufs -f Makefile.linux test

test-fsck:
	$(MAKE) -C nextufs.fsck -f Makefile.linux repair-smoke

test-mkimg:
	$(MAKE) -C nextufs.mkimg -f Makefile.linux test

test-resize:
	$(MAKE) -C nextufs.resize -f Makefile.linux test

test-fsck: test-nextufs
test-mkimg: test-fsck
test-resize: test-mkimg

clean:
	$(MAKE) -C nextufs -f Makefile.linux clean
	$(MAKE) -C nextufs.fsck -f Makefile.linux clean
	$(MAKE) -C nextufs.mkimg -f Makefile.linux clean
	$(MAKE) -C nextufs.resize -f Makefile.linux clean

install: nextufs
	$(INSTALL) -d "$(DESTDIR)$(bindir)"
	$(INSTALL) -m 0755 nextufs/nextufs "$(DESTDIR)$(bindir)/nextufs"

uninstall:
	rm -f "$(DESTDIR)$(bindir)/nextufs"
	rm -f "$(DESTDIR)$(bindir)/nextufs_probe"
	rm -f "$(DESTDIR)$(bindir)/nextufs.fsck"
	rm -f "$(DESTDIR)$(bindir)/nextufs.mkimg"
	rm -f "$(DESTDIR)$(bindir)/nextufs.resize"
