.PHONY: all clean test test-write test-write-big test-write-grow test-unlink test-mkdir test-rewrite test-link-symlink test-rmdir test-meta test-rename test-truncate test-special test-fuse-write test-permissions test-failure fsck fsck-modern mkfs nextufs

all: nextufs fsck fsck-modern mkfs

nextufs:
	$(MAKE) -C nextufs -f Makefile.linux

fsck:
	$(MAKE) -C fsck -f Makefile.linux

fsck-modern:
	$(MAKE) -C fsck.modern -f Makefile.linux

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
	$(MAKE) -C fsck -f Makefile.linux clean
	$(MAKE) -C fsck.modern -f Makefile.linux clean
	$(MAKE) -C mkfs -f Makefile.linux clean
