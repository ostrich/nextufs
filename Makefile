.PHONY: all clean test test-write test-write-big test-write-grow test-unlink test-mkdir test-rewrite test-link-symlink test-rmdir test-meta fsck mkfs nextufs

all: nextufs fsck mkfs

nextufs:
	$(MAKE) -C nextufs -f Makefile.linux

fsck:
	$(MAKE) -C fsck -f Makefile.linux

mkfs:
	$(MAKE) -C mkfs -f Makefile.linux

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

clean:
	$(MAKE) -C nextufs -f Makefile.linux clean
	$(MAKE) -C fsck -f Makefile.linux clean
	$(MAKE) -C mkfs -f Makefile.linux clean
