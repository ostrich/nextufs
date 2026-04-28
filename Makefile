CC = cc
prefix ?= /usr/local
bindir ?= $(prefix)/bin
mandir ?= $(prefix)/share/man
DESTDIR ?=
INSTALL ?= install
CFLAGS = -Iinclude -Isrc -O2 -g -std=gnu99 -Wall -Wextra -Werror
FORMAT_CFLAGS ?= -O2 -g -std=gnu89 -Wall -Wextra
FORMAT_CPPFLAGS = -DNeXT -DNeXT_MOD -DNeXT_NFS -Isrc/mkimg_format/include \
	-Disblock=mkimg_isblock -Dclrblock=mkimg_clrblock \
	-Dsetblock=mkimg_setblock -Dswap_superblock=mkimg_swap_superblock
FSCK_CFLAGS ?= -O2 -g -std=gnu99 -fcommon -Wall -Wextra
FSCK_CPPFLAGS = -DNeXT=1 -DNeXT_MOD=1 -DFASTLINK=1 \
	-Isrc/fsck/include -Iinclude
SCRATCH_DIR = $(CURDIR)/.scratch
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
export TMPDIR = $(SCRATCH_DIR)
TEST_IMAGE ?= .scratch/openstep42-base.raw
FSCK_BIN = ./nextufs fsck
MKIMG_BIN = ./nextufs mkimg
FUSE_CFLAGS != pkg-config --cflags fuse3
FUSE_LIBS != pkg-config --libs fuse3
LIB_SRCS = src/core/image.c src/core/directory.c src/core/path.c src/core/node.c \
	src/core/layout.c src/core/alloc.c src/core/label.c src/core/size.c \
	src/core/source.c src/core/info.c src/core/report.c
LIB_OBJS = $(LIB_SRCS:%.c=$(OBJ_DIR)/%.o)
LIB = $(BUILD_DIR)/libnextufs.a
WRITE_SRCS = src/mutate/dir_mutate.c src/mutate/mutate.c
WRITE_OBJS = $(WRITE_SRCS:%.c=$(OBJ_DIR)/%.o)
WRITE_LIB = $(BUILD_DIR)/libnextufs_mutate.a
COMMAND_OBJS = $(OBJ_DIR)/src/commands/main.o \
	$(OBJ_DIR)/src/commands/mount.o \
	$(OBJ_DIR)/src/commands/info.o \
	$(OBJ_DIR)/src/commands/browse.o \
	$(OBJ_DIR)/src/commands/fsck.o \
	$(OBJ_DIR)/src/commands/mkfile_cli.o \
	$(OBJ_DIR)/src/commands/mkimg.o \
	$(OBJ_DIR)/src/commands/resize.o
STRESS_OBJ = $(OBJ_DIR)/src/commands/stress.o
TEST_OBJ = $(OBJ_DIR)/tests/nextufs/nextufs_test.o
FORMAT_OBJS = $(OBJ_DIR)/src/mkimg_format/format.o \
	$(OBJ_DIR)/src/mkimg_format/format_fsinit.o \
	$(OBJ_DIR)/src/mkimg_format/format_io.o
FSCK_SRCS = alloc_map.c buffer.c byteorder.c device.c dir_repair.c \
	dir_scan.c driver.c frag_support.c inode_ops.c inode_scan.c operator.c \
	pass1.c pass1b.c pass2.c pass3.c pass4.c pass5.c session.c setup.c \
	source.c state.c
FSCK_OBJS = $(FSCK_SRCS:%.c=$(OBJ_DIR)/src/fsck/%.o)
PUBLIC_HDRS = include/nextufs.h include/nextufs_image.h include/nextufs_node.h \
	include/nextufs_mutate.h include/nextufs_info.h include/nextufs_label.h \
	include/nextufs_report.h include/nextufs_size.h
INTERNAL_HDRS = $(PUBLIC_HDRS) include/nextufs_internal.h

.PHONY: all clean install uninstall test test-nextufs test-cli-contract test-fsck test-mkimg test-resize test-write test-write-big test-write-grow test-unlink test-mkdir test-rewrite test-link-symlink test-rmdir test-meta test-rename test-truncate test-special test-fuse-write test-permissions test-failure test-stress test-stress-base test-stress-batch test-stress-fuse repair-tools repair-corpus repair-lab repair-smoke repair-repair-all scratch-dir

all: scratch-dir $(LIB) $(WRITE_LIB) nextufs nextufs_test

scratch-dir:
	mkdir -p $(SCRATCH_DIR)

$(LIB): $(LIB_OBJS)
	@mkdir -p $(@D)
	ar rcs $@ $(LIB_OBJS)

$(WRITE_LIB): $(WRITE_OBJS)
	@mkdir -p $(@D)
	ar rcs $@ $(WRITE_OBJS)

nextufs: $(COMMAND_OBJS) $(FORMAT_OBJS) $(FSCK_OBJS) $(LIB) $(WRITE_LIB)
	$(CC) $(CFLAGS) $(FUSE_CFLAGS) -o $@ $(COMMAND_OBJS) \
		$(FORMAT_OBJS) $(FSCK_OBJS) $(WRITE_LIB) $(LIB) $(FUSE_LIBS)

nextufs_test: $(TEST_OBJ) $(LIB)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJ) $(LIB)

nextufs_stress: $(STRESS_OBJ) $(LIB) $(WRITE_LIB)
	$(CC) $(CFLAGS) -o $@ $(STRESS_OBJ) $(WRITE_LIB) $(LIB)

$(LIB_OBJS): $(INTERNAL_HDRS)
$(WRITE_OBJS): $(INTERNAL_HDRS)
$(TEST_OBJ): tests/nextufs/nextufs_test.c $(PUBLIC_HDRS)
$(STRESS_OBJ): src/commands/stress.c $(PUBLIC_HDRS)

$(OBJ_DIR)/src/commands/mount.o: src/commands/mount.c $(INTERNAL_HDRS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(FUSE_CFLAGS) -c -o $@ $<

$(OBJ_DIR)/src/commands/info.o: src/commands/info.c $(PUBLIC_HDRS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/src/commands/browse.o: src/commands/browse.c $(PUBLIC_HDRS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/src/commands/fsck.o: src/commands/fsck.c src/commands/commands.h include/nextufs_fsck.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/src/commands/mkfile_cli.o: src/commands/mkfile.c $(PUBLIC_HDRS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/src/commands/mkimg.o: src/commands/mkimg.c $(PUBLIC_HDRS) src/mkimg_format/format.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(FORMAT_CPPFLAGS) \
		-Isrc/mkimg_format -Iinclude -c -o $@ $<

$(OBJ_DIR)/src/commands/resize.o: src/commands/resize.c $(INTERNAL_HDRS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/src/mkimg_format/format.o: src/mkimg_format/format.c
	@mkdir -p $(@D)
	$(CC) $(FORMAT_CPPFLAGS) $(FORMAT_CFLAGS) -Isrc/mkimg_format -c -o $@ $<

$(OBJ_DIR)/src/mkimg_format/format_fsinit.o: src/mkimg_format/format_fsinit.c
	@mkdir -p $(@D)
	$(CC) $(FORMAT_CPPFLAGS) $(FORMAT_CFLAGS) -Isrc/mkimg_format -c -o $@ $<

$(OBJ_DIR)/src/mkimg_format/format_io.o: src/mkimg_format/format_io.c
	@mkdir -p $(@D)
	$(CC) $(FORMAT_CPPFLAGS) $(FORMAT_CFLAGS) -Isrc/mkimg_format -c -o $@ $<

$(OBJ_DIR)/src/fsck/%.o: src/fsck/%.c src/fsck/fsck.h include/nextufs_image.h include/nextufs_fsck.h
	@mkdir -p $(@D)
	$(CC) $(FSCK_CPPFLAGS) $(FSCK_CFLAGS) -c -o $@ $<

$(OBJ_DIR)/%.o: %.c $(INTERNAL_HDRS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

test: test-nextufs test-fsck

test-nextufs: all test-mkimg test-resize test-cli-contract
	./nextufs --help >/dev/null
	./nextufs --version >/dev/null
	./nextufs info --help >/dev/null
	./nextufs browse --help >/dev/null
	./nextufs resize --help >/dev/null
	./nextufs resize grow --help >/dev/null
	./nextufs info $(TEST_IMAGE) >/dev/null
	./nextufs browse $(TEST_IMAGE) >/dev/null
	./nextufs info --json $(TEST_IMAGE) >/dev/null
	if ./nextufs --json info $(TEST_IMAGE) >/dev/null 2>&1; then exit 1; fi
	if ./nextufs --json fsck -n $(TEST_IMAGE) >/dev/null 2>&1; then exit 1; fi
	./nextufs fsck -n $(TEST_IMAGE) >/dev/null
	./nextufs mkimg --dry-run $(SCRATCH_DIR)/nextufs-cli-mkimg.img 64M >/dev/null
	./nextufs_test $(TEST_IMAGE)
	./tests/nextufs/test_fuse.sh $(TEST_IMAGE)

test-cli-contract: all
	sh tests/nextufs/test_cli_contract.sh $(SCRATCH_DIR)

test-fsck: test-nextufs repair-smoke

test-mkimg: all
	rm -f $(SCRATCH_DIR)/mkimg-raw.img $(SCRATCH_DIR)/mkimg-labeled.img \
		$(SCRATCH_DIR)/mkimg-raw-full.img \
		$(SCRATCH_DIR)/mkimg-raw-a.img $(SCRATCH_DIR)/mkimg-raw-b.img \
		$(SCRATCH_DIR)/mkimg-labeled-a.img $(SCRATCH_DIR)/mkimg-labeled-b.img \
		$(SCRATCH_DIR)/mkimg-overwrite.img $(SCRATCH_DIR)/mkimg-dry-run.img \
		$(SCRATCH_DIR)/mkimg-limit.raw $(SCRATCH_DIR)/mkimg-too-large.raw \
		$(SCRATCH_DIR)/mkimg-invalid.raw \
		$(SCRATCH_DIR)/mkimg-labeled.inspect $(SCRATCH_DIR)/mkimg-overwrite.err \
		$(SCRATCH_DIR)/mkimg-raw-label.err $(SCRATCH_DIR)/mkimg-invalid.err \
		$(SCRATCH_DIR)/mkimg-too-large.err
	./nextufs mkimg --raw $(SCRATCH_DIR)/mkimg-raw.img 64M >/dev/null
	$(FSCK_BIN) -n $(SCRATCH_DIR)/mkimg-raw.img >/dev/null
	./nextufs mkimg $(SCRATCH_DIR)/mkimg-labeled.img 64M >/dev/null
	$(FSCK_BIN) -n $(SCRATCH_DIR)/mkimg-labeled.img >/dev/null
	./nextufs info $(SCRATCH_DIR)/mkimg-labeled.img >$(SCRATCH_DIR)/mkimg-labeled.inspect
	grep -F 'Disk Label' $(SCRATCH_DIR)/mkimg-labeled.inspect >/dev/null
	grep -F 'slice base                     0x28000 (163840)' $(SCRATCH_DIR)/mkimg-labeled.inspect >/dev/null
	grep -F 'superblock base                0x2a000 (172032)' $(SCRATCH_DIR)/mkimg-labeled.inspect >/dev/null
	./nextufs mkimg --raw $(SCRATCH_DIR)/mkimg-raw-full.img 65536 63 16 8192 1024 16 10 60 2048 t >/dev/null
	$(FSCK_BIN) -n $(SCRATCH_DIR)/mkimg-raw-full.img >/dev/null
	./nextufs mkimg --raw $(SCRATCH_DIR)/mkimg-raw-a.img 64M >/dev/null
	./nextufs mkimg --raw $(SCRATCH_DIR)/mkimg-raw-b.img 64M >/dev/null
	cmp $(SCRATCH_DIR)/mkimg-raw-a.img $(SCRATCH_DIR)/mkimg-raw-b.img
	./nextufs mkimg --label repro $(SCRATCH_DIR)/mkimg-labeled-a.img 64M >/dev/null
	./nextufs mkimg --label repro $(SCRATCH_DIR)/mkimg-labeled-b.img 64M >/dev/null
	cmp $(SCRATCH_DIR)/mkimg-labeled-a.img $(SCRATCH_DIR)/mkimg-labeled-b.img
	./nextufs mkimg --raw $(SCRATCH_DIR)/mkimg-overwrite.img 64M >/dev/null
	! ./nextufs mkimg --raw $(SCRATCH_DIR)/mkimg-overwrite.img 64M >/dev/null 2>$(SCRATCH_DIR)/mkimg-overwrite.err
	grep -F 'cannot create' $(SCRATCH_DIR)/mkimg-overwrite.err >/dev/null
	./nextufs mkimg --force-overwrite --raw $(SCRATCH_DIR)/mkimg-overwrite.img 64M >/dev/null
	./nextufs mkimg --dry-run $(SCRATCH_DIR)/mkimg-dry-run.img 64M >/dev/null
	test ! -e $(SCRATCH_DIR)/mkimg-dry-run.img
	! ./nextufs mkimg --raw --label bad $(SCRATCH_DIR)/mkimg-invalid.raw 64M >/dev/null 2>$(SCRATCH_DIR)/mkimg-raw-label.err
	grep -F -- '--label is not valid with --raw' $(SCRATCH_DIR)/mkimg-raw-label.err >/dev/null
	! ./nextufs mkimg --raw $(SCRATCH_DIR)/mkimg-invalid.raw 64M 0 16 8192 1024 16 10 60 2048 t >/dev/null 2>$(SCRATCH_DIR)/mkimg-invalid.err
	grep -F 'preposterous nsect' $(SCRATCH_DIR)/mkimg-invalid.err >/dev/null
	./nextufs mkimg --raw $(SCRATCH_DIR)/mkimg-limit.raw 4194176 >/dev/null
	$(FSCK_BIN) -n $(SCRATCH_DIR)/mkimg-limit.raw >/dev/null
	! ./nextufs mkimg --raw $(SCRATCH_DIR)/mkimg-too-large.raw 4194177 >/dev/null 2>$(SCRATCH_DIR)/mkimg-too-large.err
	grep -F 'compatibility limit' $(SCRATCH_DIR)/mkimg-too-large.err >/dev/null
	./nextufs info --json $(SCRATCH_DIR)/mkimg-labeled.img >/dev/null

test-resize: all
	./nextufs resize --help >/dev/null
	./nextufs resize grow --help >/dev/null

test-write: all
	cp --reflink=auto $(TEST_IMAGE) $(SCRATCH_DIR)/nextufs-write-test.raw
	./nextufs mkfile $(SCRATCH_DIR)/nextufs-write-test.raw /private/tmp/nextufs-write-test 'hello from linux nextufs'
	./nextufs browse $(SCRATCH_DIR)/nextufs-write-test.raw /private/tmp/nextufs-write-test >$(SCRATCH_DIR)/nextufs_write_browse.out
	grep -F "lookup '/private/tmp/nextufs-write-test':" $(SCRATCH_DIR)/nextufs_write_browse.out
	grep -F 'hello from linux' $(SCRATCH_DIR)/nextufs_write_browse.out
	grep -F 'nextufs' $(SCRATCH_DIR)/nextufs_write_browse.out
	dd if=$(SCRATCH_DIR)/nextufs-write-test.raw of=$(SCRATCH_DIR)/nextufs-write-test-a.raw bs=1024 skip=160 count=2096480 status=none
	$(FSCK_BIN) -n $(SCRATCH_DIR)/nextufs-write-test-a.raw >$(SCRATCH_DIR)/nextufs_write_fsck.out
	grep -F '** Phase 5 - Check Cyl groups' $(SCRATCH_DIR)/nextufs_write_fsck.out

test-write-big: all
	cp --reflink=auto $(TEST_IMAGE) $(SCRATCH_DIR)/nextufs-write-big.raw
	awk 'BEGIN { for (i = 0; i < 3000; i++) printf "%c", 65 + (i % 26) }' >$(SCRATCH_DIR)/nextufs-big-input.bin
	./nextufs mkfile --from-file $(SCRATCH_DIR)/nextufs-write-big.raw /private/tmp/nextufs-write-big $(SCRATCH_DIR)/nextufs-big-input.bin
	./nextufs browse $(SCRATCH_DIR)/nextufs-write-big.raw /private/tmp/nextufs-write-big >$(SCRATCH_DIR)/nextufs_write_big_browse.out
	grep -F "lookup '/private/tmp/nextufs-write-big':" $(SCRATCH_DIR)/nextufs_write_big_browse.out
	grep -F 'size=3000' $(SCRATCH_DIR)/nextufs_write_big_browse.out
	mkdir -p $(SCRATCH_DIR)/nextufs-write-big-mnt
	./nextufs mount $(SCRATCH_DIR)/nextufs-write-big.raw $(SCRATCH_DIR)/nextufs-write-big-mnt -o rw -f -s >$(SCRATCH_DIR)/nextufs_write_big_fuse.log 2>&1 & \
		pid=$$!; \
		trap 'kill $$pid 2>/dev/null || true; fusermount3 -u $(SCRATCH_DIR)/nextufs-write-big-mnt >/dev/null 2>&1 || true' EXIT INT TERM; \
		sleep 1; \
		cmp $(SCRATCH_DIR)/nextufs-big-input.bin $(SCRATCH_DIR)/nextufs-write-big-mnt/private/tmp/nextufs-write-big; \
		fusermount3 -u $(SCRATCH_DIR)/nextufs-write-big-mnt; \
		wait $$pid || true; \
		trap - EXIT INT TERM
	dd if=$(SCRATCH_DIR)/nextufs-write-big.raw of=$(SCRATCH_DIR)/nextufs-write-big-a.raw bs=1024 skip=160 count=2096480 status=none
	$(FSCK_BIN) -n $(SCRATCH_DIR)/nextufs-write-big-a.raw >$(SCRATCH_DIR)/nextufs_write_big_fsck.out
	grep -F '** Phase 5 - Check Cyl groups' $(SCRATCH_DIR)/nextufs_write_big_fsck.out

test-write-grow: all
	cp --reflink=auto $(TEST_IMAGE) $(SCRATCH_DIR)/nextufs-write-grow.raw
	i=0; while [ $$i -lt 120 ]; do \
		./nextufs mkfile $(SCRATCH_DIR)/nextufs-write-grow.raw /private/tmp/nextufs-grow-$$i '' || exit $$?; \
		i=`expr $$i + 1`; \
	done
	./nextufs browse $(SCRATCH_DIR)/nextufs-write-grow.raw /private/tmp >$(SCRATCH_DIR)/nextufs_write_grow_browse.out
	grep -F 'size=3072' $(SCRATCH_DIR)/nextufs_write_grow_browse.out
	grep -F "nextufs-grow-119" $(SCRATCH_DIR)/nextufs_write_grow_browse.out
	mkdir -p $(SCRATCH_DIR)/nextufs-write-grow-mnt
	./nextufs mount $(SCRATCH_DIR)/nextufs-write-grow.raw $(SCRATCH_DIR)/nextufs-write-grow-mnt -o rw -f -s >$(SCRATCH_DIR)/nextufs_write_grow_fuse.log 2>&1 & \
		pid=$$!; \
		trap 'kill $$pid 2>/dev/null || true; fusermount3 -u $(SCRATCH_DIR)/nextufs-write-grow-mnt >/dev/null 2>&1 || true' EXIT INT TERM; \
		sleep 1; \
		test -f $(SCRATCH_DIR)/nextufs-write-grow-mnt/private/tmp/nextufs-grow-119; \
		fusermount3 -u $(SCRATCH_DIR)/nextufs-write-grow-mnt; \
		wait $$pid || true; \
		trap - EXIT INT TERM
	dd if=$(SCRATCH_DIR)/nextufs-write-grow.raw of=$(SCRATCH_DIR)/nextufs-write-grow-a.raw bs=1024 skip=160 count=2096480 status=none
	$(FSCK_BIN) -n $(SCRATCH_DIR)/nextufs-write-grow-a.raw >$(SCRATCH_DIR)/nextufs_write_grow_fsck.out
	grep -F '** Phase 5 - Check Cyl groups' $(SCRATCH_DIR)/nextufs_write_grow_fsck.out

test-unlink: all
	cp --reflink=auto $(TEST_IMAGE) $(SCRATCH_DIR)/nextufs-unlink.raw
	./nextufs mkfile $(SCRATCH_DIR)/nextufs-unlink.raw /private/tmp/nextufs-unlink-test 'unlink me'
	./nextufs mkfile --unlink $(SCRATCH_DIR)/nextufs-unlink.raw /private/tmp/nextufs-unlink-test
	./nextufs browse $(SCRATCH_DIR)/nextufs-unlink.raw /private/tmp/nextufs-unlink-test >$(SCRATCH_DIR)/nextufs_unlink_browse.out 2>&1
	grep -F "lookup '/private/tmp/nextufs-unlink-test' failed" $(SCRATCH_DIR)/nextufs_unlink_browse.out
	mkdir -p $(SCRATCH_DIR)/nextufs-unlink-mnt
	./nextufs mount $(SCRATCH_DIR)/nextufs-unlink.raw $(SCRATCH_DIR)/nextufs-unlink-mnt -o rw -f -s >$(SCRATCH_DIR)/nextufs_unlink_fuse.log 2>&1 & \
		pid=$$!; \
		trap 'kill $$pid 2>/dev/null || true; fusermount3 -u $(SCRATCH_DIR)/nextufs-unlink-mnt >/dev/null 2>&1 || true' EXIT INT TERM; \
		sleep 1; \
		test ! -e $(SCRATCH_DIR)/nextufs-unlink-mnt/private/tmp/nextufs-unlink-test; \
		fusermount3 -u $(SCRATCH_DIR)/nextufs-unlink-mnt; \
		wait $$pid || true; \
		trap - EXIT INT TERM
	dd if=$(SCRATCH_DIR)/nextufs-unlink.raw of=$(SCRATCH_DIR)/nextufs-unlink-a.raw bs=1024 skip=160 count=2096480 status=none
	$(FSCK_BIN) -n $(SCRATCH_DIR)/nextufs-unlink-a.raw >$(SCRATCH_DIR)/nextufs_unlink_fsck.out
	grep -F '** Phase 5 - Check Cyl groups' $(SCRATCH_DIR)/nextufs_unlink_fsck.out

test-mkdir: all
	cp --reflink=auto $(TEST_IMAGE) $(SCRATCH_DIR)/nextufs-mkdir.raw
	./nextufs mkfile --mkdir $(SCRATCH_DIR)/nextufs-mkdir.raw /private/tmp/nextufs-dir-test
	./nextufs mkfile $(SCRATCH_DIR)/nextufs-mkdir.raw /private/tmp/nextufs-dir-test/child 'inside dir'
	./nextufs browse $(SCRATCH_DIR)/nextufs-mkdir.raw /private/tmp/nextufs-dir-test >$(SCRATCH_DIR)/nextufs_mkdir_browse_dir.out
	grep -F "lookup '/private/tmp/nextufs-dir-test':" $(SCRATCH_DIR)/nextufs_mkdir_browse_dir.out
	grep -F "name='child'" $(SCRATCH_DIR)/nextufs_mkdir_browse_dir.out
	./nextufs browse $(SCRATCH_DIR)/nextufs-mkdir.raw /private/tmp/nextufs-dir-test/child >$(SCRATCH_DIR)/nextufs_mkdir_browse_file.out
	grep -F 'inside dir' $(SCRATCH_DIR)/nextufs_mkdir_browse_file.out
	mkdir -p $(SCRATCH_DIR)/nextufs-mkdir-mnt
	./nextufs mount $(SCRATCH_DIR)/nextufs-mkdir.raw $(SCRATCH_DIR)/nextufs-mkdir-mnt -o rw -f -s >$(SCRATCH_DIR)/nextufs_mkdir_fuse.log 2>&1 & \
		pid=$$!; \
		trap 'kill $$pid 2>/dev/null || true; fusermount3 -u $(SCRATCH_DIR)/nextufs-mkdir-mnt >/dev/null 2>&1 || true' EXIT INT TERM; \
		sleep 1; \
		test -d $(SCRATCH_DIR)/nextufs-mkdir-mnt/private/tmp/nextufs-dir-test; \
		grep -F 'inside dir' $(SCRATCH_DIR)/nextufs-mkdir-mnt/private/tmp/nextufs-dir-test/child; \
		fusermount3 -u $(SCRATCH_DIR)/nextufs-mkdir-mnt; \
		wait $$pid || true; \
		trap - EXIT INT TERM
	dd if=$(SCRATCH_DIR)/nextufs-mkdir.raw of=$(SCRATCH_DIR)/nextufs-mkdir-a.raw bs=1024 skip=160 count=2096480 status=none
	$(FSCK_BIN) -n $(SCRATCH_DIR)/nextufs-mkdir-a.raw >$(SCRATCH_DIR)/nextufs_mkdir_fsck.out
	grep -F '** Phase 5 - Check Cyl groups' $(SCRATCH_DIR)/nextufs_mkdir_fsck.out

test-rewrite: all
	cp --reflink=auto $(TEST_IMAGE) $(SCRATCH_DIR)/nextufs-rewrite.raw
	./nextufs mkfile $(SCRATCH_DIR)/nextufs-rewrite.raw /private/tmp/nextufs-rewrite-test 'start'
	./nextufs mkfile --overwrite $(SCRATCH_DIR)/nextufs-rewrite.raw /private/tmp/nextufs-rewrite-test 'middle'
	./nextufs mkfile --append $(SCRATCH_DIR)/nextufs-rewrite.raw /private/tmp/nextufs-rewrite-test '+end'
	./nextufs browse $(SCRATCH_DIR)/nextufs-rewrite.raw /private/tmp/nextufs-rewrite-test >$(SCRATCH_DIR)/nextufs_rewrite_browse.out
	grep -F 'middle+end' $(SCRATCH_DIR)/nextufs_rewrite_browse.out
	mkdir -p $(SCRATCH_DIR)/nextufs-rewrite-mnt
	./nextufs mount $(SCRATCH_DIR)/nextufs-rewrite.raw $(SCRATCH_DIR)/nextufs-rewrite-mnt -o rw -f -s >$(SCRATCH_DIR)/nextufs_rewrite_fuse.log 2>&1 & \
		pid=$$!; \
		trap 'kill $$pid 2>/dev/null || true; fusermount3 -u $(SCRATCH_DIR)/nextufs-rewrite-mnt >/dev/null 2>&1 || true' EXIT INT TERM; \
		sleep 1; \
		grep -F 'middle+end' $(SCRATCH_DIR)/nextufs-rewrite-mnt/private/tmp/nextufs-rewrite-test; \
		fusermount3 -u $(SCRATCH_DIR)/nextufs-rewrite-mnt; \
		wait $$pid || true; \
		trap - EXIT INT TERM
	dd if=$(SCRATCH_DIR)/nextufs-rewrite.raw of=$(SCRATCH_DIR)/nextufs-rewrite-a.raw bs=1024 skip=160 count=2096480 status=none
	$(FSCK_BIN) -n $(SCRATCH_DIR)/nextufs-rewrite-a.raw >$(SCRATCH_DIR)/nextufs_rewrite_fsck.out
	grep -F '** Phase 5 - Check Cyl groups' $(SCRATCH_DIR)/nextufs_rewrite_fsck.out

test-link-symlink: all
	cp --reflink=auto $(TEST_IMAGE) $(SCRATCH_DIR)/nextufs-link.raw
	./nextufs mkfile $(SCRATCH_DIR)/nextufs-link.raw /private/tmp/nextufs-link-src 'linked data'
	./nextufs mkfile --link $(SCRATCH_DIR)/nextufs-link.raw /private/tmp/nextufs-link-src /private/tmp/nextufs-link-hard
	./nextufs mkfile --symlink $(SCRATCH_DIR)/nextufs-link.raw nextufs-link-hard /private/tmp/nextufs-link-soft
	./nextufs browse $(SCRATCH_DIR)/nextufs-link.raw /private/tmp/nextufs-link-hard >$(SCRATCH_DIR)/nextufs_link_browse_hard.out
	grep -F 'linked data' $(SCRATCH_DIR)/nextufs_link_browse_hard.out
	mkdir -p $(SCRATCH_DIR)/nextufs-link-mnt
	./nextufs mount $(SCRATCH_DIR)/nextufs-link.raw $(SCRATCH_DIR)/nextufs-link-mnt -o rw -f -s >$(SCRATCH_DIR)/nextufs_link_fuse.log 2>&1 & \
		pid=$$!; \
		trap 'kill $$pid 2>/dev/null || true; fusermount3 -u $(SCRATCH_DIR)/nextufs-link-mnt >/dev/null 2>&1 || true' EXIT INT TERM; \
		sleep 1; \
		readlink $(SCRATCH_DIR)/nextufs-link-mnt/private/tmp/nextufs-link-soft | grep -F 'nextufs-link-hard'; \
		grep -F 'linked data' $(SCRATCH_DIR)/nextufs-link-mnt/private/tmp/nextufs-link-soft; \
		fusermount3 -u $(SCRATCH_DIR)/nextufs-link-mnt; \
		wait $$pid || true; \
		trap - EXIT INT TERM
	dd if=$(SCRATCH_DIR)/nextufs-link.raw of=$(SCRATCH_DIR)/nextufs-link-a.raw bs=1024 skip=160 count=2096480 status=none
	$(FSCK_BIN) -n $(SCRATCH_DIR)/nextufs-link-a.raw >$(SCRATCH_DIR)/nextufs_link_fsck.out
	grep -F '** Phase 5 - Check Cyl groups' $(SCRATCH_DIR)/nextufs_link_fsck.out

test-rmdir: all
	cp --reflink=auto $(TEST_IMAGE) $(SCRATCH_DIR)/nextufs-rmdir.raw
	./nextufs mkfile --mkdir $(SCRATCH_DIR)/nextufs-rmdir.raw /private/tmp/nextufs-rmdir-test
	./nextufs mkfile --rmdir $(SCRATCH_DIR)/nextufs-rmdir.raw /private/tmp/nextufs-rmdir-test
	./nextufs browse $(SCRATCH_DIR)/nextufs-rmdir.raw /private/tmp/nextufs-rmdir-test >$(SCRATCH_DIR)/nextufs_rmdir_browse.out 2>&1
	grep -F "lookup '/private/tmp/nextufs-rmdir-test' failed" $(SCRATCH_DIR)/nextufs_rmdir_browse.out
	mkdir -p $(SCRATCH_DIR)/nextufs-rmdir-mnt
	./nextufs mount $(SCRATCH_DIR)/nextufs-rmdir.raw $(SCRATCH_DIR)/nextufs-rmdir-mnt -o rw -f -s >$(SCRATCH_DIR)/nextufs_rmdir_fuse.log 2>&1 & \
		pid=$$!; \
		trap 'kill $$pid 2>/dev/null || true; fusermount3 -u $(SCRATCH_DIR)/nextufs-rmdir-mnt >/dev/null 2>&1 || true' EXIT INT TERM; \
		sleep 1; \
		test ! -e $(SCRATCH_DIR)/nextufs-rmdir-mnt/private/tmp/nextufs-rmdir-test; \
		fusermount3 -u $(SCRATCH_DIR)/nextufs-rmdir-mnt; \
		wait $$pid || true; \
		trap - EXIT INT TERM
	dd if=$(SCRATCH_DIR)/nextufs-rmdir.raw of=$(SCRATCH_DIR)/nextufs-rmdir-a.raw bs=1024 skip=160 count=2096480 status=none
	$(FSCK_BIN) -n $(SCRATCH_DIR)/nextufs-rmdir-a.raw >$(SCRATCH_DIR)/nextufs_rmdir_fsck.out
	grep -F '** Phase 5 - Check Cyl groups' $(SCRATCH_DIR)/nextufs_rmdir_fsck.out

test-meta: all
	cp --reflink=auto $(TEST_IMAGE) $(SCRATCH_DIR)/nextufs-meta.raw
	./nextufs mkfile $(SCRATCH_DIR)/nextufs-meta.raw /private/tmp/nextufs-meta-test 'meta'
	./nextufs mkfile --chmod $(SCRATCH_DIR)/nextufs-meta.raw /private/tmp/nextufs-meta-test 0600
	./nextufs mkfile --chown $(SCRATCH_DIR)/nextufs-meta.raw /private/tmp/nextufs-meta-test 123 456
	./nextufs mkfile --utimes $(SCRATCH_DIR)/nextufs-meta.raw /private/tmp/nextufs-meta-test 111111111 222222222
	./nextufs browse $(SCRATCH_DIR)/nextufs-meta.raw /private/tmp/nextufs-meta-test >$(SCRATCH_DIR)/nextufs_meta_browse.out
	grep -F 'mode=0100600' $(SCRATCH_DIR)/nextufs_meta_browse.out
	grep -F 'uid=123 gid=456' $(SCRATCH_DIR)/nextufs_meta_browse.out
	dd if=$(SCRATCH_DIR)/nextufs-meta.raw of=$(SCRATCH_DIR)/nextufs-meta-a.raw bs=1024 skip=160 count=2096480 status=none
	$(FSCK_BIN) -n $(SCRATCH_DIR)/nextufs-meta-a.raw >$(SCRATCH_DIR)/nextufs_meta_fsck.out
	grep -F '** Phase 5 - Check Cyl groups' $(SCRATCH_DIR)/nextufs_meta_fsck.out

test-rename: all
	cp --reflink=auto $(TEST_IMAGE) $(SCRATCH_DIR)/nextufs-rename.raw
	./nextufs mkfile --mkdir $(SCRATCH_DIR)/nextufs-rename.raw /private/tmp/nextufs-rename-a
	./nextufs mkfile --mkdir $(SCRATCH_DIR)/nextufs-rename.raw /private/tmp/nextufs-rename-b
	./nextufs mkfile $(SCRATCH_DIR)/nextufs-rename.raw /private/tmp/nextufs-rename-a/file 'rename payload'
	./nextufs mkfile $(SCRATCH_DIR)/nextufs-rename.raw /private/tmp/nextufs-rename-b/existing 'replace me'
	./nextufs mkfile --rename $(SCRATCH_DIR)/nextufs-rename.raw /private/tmp/nextufs-rename-a/file /private/tmp/nextufs-rename-b/file
	./nextufs mkfile --rename $(SCRATCH_DIR)/nextufs-rename.raw /private/tmp/nextufs-rename-b/file /private/tmp/nextufs-rename-b/existing
	./nextufs mkfile --mkdir $(SCRATCH_DIR)/nextufs-rename.raw /private/tmp/nextufs-rename-a/subdir
	./nextufs mkfile --rename $(SCRATCH_DIR)/nextufs-rename.raw /private/tmp/nextufs-rename-a/subdir /private/tmp/nextufs-rename-b/subdir
	./nextufs browse $(SCRATCH_DIR)/nextufs-rename.raw /private/tmp/nextufs-rename-b/existing >$(SCRATCH_DIR)/nextufs_rename_browse_file.out
	grep -F 'rename payload' $(SCRATCH_DIR)/nextufs_rename_browse_file.out
	./nextufs browse $(SCRATCH_DIR)/nextufs-rename.raw /private/tmp/nextufs-rename-b/subdir >$(SCRATCH_DIR)/nextufs_rename_browse_dir.out
	grep -F "lookup '/private/tmp/nextufs-rename-b/subdir':" $(SCRATCH_DIR)/nextufs_rename_browse_dir.out
	mkdir -p $(SCRATCH_DIR)/nextufs-rename-mnt
	./nextufs mount $(SCRATCH_DIR)/nextufs-rename.raw $(SCRATCH_DIR)/nextufs-rename-mnt -o rw -f -s >$(SCRATCH_DIR)/nextufs_rename_fuse.log 2>&1 & \
		pid=$$!; \
		trap 'kill $$pid 2>/dev/null || true; fusermount3 -u $(SCRATCH_DIR)/nextufs-rename-mnt >/dev/null 2>&1 || true' EXIT INT TERM; \
		sleep 1; \
		test ! -e $(SCRATCH_DIR)/nextufs-rename-mnt/private/tmp/nextufs-rename-a/file; \
		grep -F 'rename payload' $(SCRATCH_DIR)/nextufs-rename-mnt/private/tmp/nextufs-rename-b/existing; \
		test -d $(SCRATCH_DIR)/nextufs-rename-mnt/private/tmp/nextufs-rename-b/subdir; \
		fusermount3 -u $(SCRATCH_DIR)/nextufs-rename-mnt; \
		wait $$pid || true; \
		trap - EXIT INT TERM
	dd if=$(SCRATCH_DIR)/nextufs-rename.raw of=$(SCRATCH_DIR)/nextufs-rename-a.raw bs=1024 skip=160 count=2096480 status=none
	$(FSCK_BIN) -n $(SCRATCH_DIR)/nextufs-rename-a.raw >$(SCRATCH_DIR)/nextufs_rename_fsck.out
	grep -F '** Phase 5 - Check Cyl groups' $(SCRATCH_DIR)/nextufs_rename_fsck.out

test-truncate: all
	cp --reflink=auto $(TEST_IMAGE) $(SCRATCH_DIR)/nextufs-truncate.raw
	awk 'BEGIN { for (i = 0; i < 120000; i++) printf "%c", 65 + (i % 26) }' >$(SCRATCH_DIR)/nextufs-truncate-input.bin
	printf '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0' >$(SCRATCH_DIR)/nextufs-zero-16.bin
	printf 'tail' >$(SCRATCH_DIR)/nextufs-tail.bin
	./nextufs mkfile --from-file $(SCRATCH_DIR)/nextufs-truncate.raw /private/tmp/nextufs-truncate $(SCRATCH_DIR)/nextufs-truncate-input.bin
	./nextufs browse $(SCRATCH_DIR)/nextufs-truncate.raw /private/tmp/nextufs-truncate >$(SCRATCH_DIR)/nextufs_truncate_browse_big.out
	grep -F 'size=120000' $(SCRATCH_DIR)/nextufs_truncate_browse_big.out
	grep -E 'indirect blocks: [0-9]' $(SCRATCH_DIR)/nextufs_truncate_browse_big.out
	./nextufs mkfile --truncate $(SCRATCH_DIR)/nextufs-truncate.raw /private/tmp/nextufs-truncate 4096
	./nextufs browse $(SCRATCH_DIR)/nextufs-truncate.raw /private/tmp/nextufs-truncate >$(SCRATCH_DIR)/nextufs_truncate_browse_small.out
	grep -F 'size=4096' $(SCRATCH_DIR)/nextufs_truncate_browse_small.out
	./nextufs mkfile --truncate $(SCRATCH_DIR)/nextufs-truncate.raw /private/tmp/nextufs-truncate 16384
	./nextufs mkfile --pwrite $(SCRATCH_DIR)/nextufs-truncate.raw /private/tmp/nextufs-truncate 12288 tail
	mkdir -p $(SCRATCH_DIR)/nextufs-truncate-mnt
	./nextufs mount $(SCRATCH_DIR)/nextufs-truncate.raw $(SCRATCH_DIR)/nextufs-truncate-mnt -o rw -f -s >$(SCRATCH_DIR)/nextufs_truncate_fuse.log 2>&1 & \
		pid=$$!; \
		trap 'kill $$pid 2>/dev/null || true; fusermount3 -u $(SCRATCH_DIR)/nextufs-truncate-mnt >/dev/null 2>&1 || true' EXIT INT TERM; \
		sleep 1; \
		stat -c '%s' $(SCRATCH_DIR)/nextufs-truncate-mnt/private/tmp/nextufs-truncate | grep -Fx '16384'; \
		dd if=$(SCRATCH_DIR)/nextufs-truncate-input.bin of=$(SCRATCH_DIR)/nextufs-truncate-expected.bin bs=1 count=4096 status=none; \
		dd if=$(SCRATCH_DIR)/nextufs-truncate-mnt/private/tmp/nextufs-truncate of=$(SCRATCH_DIR)/nextufs-truncate-actual.bin bs=1 count=4096 status=none; \
		cmp $(SCRATCH_DIR)/nextufs-truncate-expected.bin $(SCRATCH_DIR)/nextufs-truncate-actual.bin; \
		dd if=$(SCRATCH_DIR)/nextufs-truncate-mnt/private/tmp/nextufs-truncate bs=1 skip=4096 count=16 status=none | cmp $(SCRATCH_DIR)/nextufs-zero-16.bin -; \
		dd if=$(SCRATCH_DIR)/nextufs-truncate-mnt/private/tmp/nextufs-truncate of=$(SCRATCH_DIR)/nextufs-tail-actual.bin bs=1 skip=12288 count=4 status=none; \
		cmp $(SCRATCH_DIR)/nextufs-tail.bin $(SCRATCH_DIR)/nextufs-tail-actual.bin; \
		fusermount3 -u $(SCRATCH_DIR)/nextufs-truncate-mnt; \
		wait $$pid || true; \
		trap - EXIT INT TERM
	dd if=$(SCRATCH_DIR)/nextufs-truncate.raw of=$(SCRATCH_DIR)/nextufs-truncate-a.raw bs=1024 skip=160 count=2096480 status=none
	$(FSCK_BIN) -n $(SCRATCH_DIR)/nextufs-truncate-a.raw >$(SCRATCH_DIR)/nextufs_truncate_fsck.out
	grep -F '** Phase 5 - Check Cyl groups' $(SCRATCH_DIR)/nextufs_truncate_fsck.out

test-special: all
	cp --reflink=auto $(TEST_IMAGE) $(SCRATCH_DIR)/nextufs-special.raw
	./nextufs mkfile --mknod $(SCRATCH_DIR)/nextufs-special.raw /private/tmp/nextufs-special-fifo 010644 0
	./nextufs mkfile --mknod $(SCRATCH_DIR)/nextufs-special.raw /private/tmp/nextufs-special-char 020600 259
	./nextufs browse $(SCRATCH_DIR)/nextufs-special.raw /private/tmp/nextufs-special-char >$(SCRATCH_DIR)/nextufs_special_browse_char.out
	grep -F 'mode=020600' $(SCRATCH_DIR)/nextufs_special_browse_char.out
	grep -F 'rdev:         259' $(SCRATCH_DIR)/nextufs_special_browse_char.out
	mkdir -p $(SCRATCH_DIR)/nextufs-special-mnt
	./nextufs mount $(SCRATCH_DIR)/nextufs-special.raw $(SCRATCH_DIR)/nextufs-special-mnt -o rw -f -s >$(SCRATCH_DIR)/nextufs_special_fuse.log 2>&1 & \
		pid=$$!; \
		trap 'kill $$pid 2>/dev/null || true; fusermount3 -u $(SCRATCH_DIR)/nextufs-special-mnt >/dev/null 2>&1 || true' EXIT INT TERM; \
		sleep 1; \
		test -p $(SCRATCH_DIR)/nextufs-special-mnt/private/tmp/nextufs-special-fifo; \
		stat -c '%F' $(SCRATCH_DIR)/nextufs-special-mnt/private/tmp/nextufs-special-char | grep -F 'character special file'; \
		fusermount3 -u $(SCRATCH_DIR)/nextufs-special-mnt; \
		wait $$pid || true; \
		trap - EXIT INT TERM
	dd if=$(SCRATCH_DIR)/nextufs-special.raw of=$(SCRATCH_DIR)/nextufs-special-a.raw bs=1024 skip=160 count=2096480 status=none
	$(FSCK_BIN) -n $(SCRATCH_DIR)/nextufs-special-a.raw >$(SCRATCH_DIR)/nextufs_special_fsck.out
	grep -F '** Phase 5 - Check Cyl groups' $(SCRATCH_DIR)/nextufs_special_fsck.out

test-fuse-write: all
	cp --reflink=auto $(TEST_IMAGE) $(SCRATCH_DIR)/nextufs-fuse-write.raw
	mkdir -p $(SCRATCH_DIR)/nextufs-fuse-write-mnt
	printf 'tail' >$(SCRATCH_DIR)/nextufs-fuse-tail.bin
	./nextufs mount $(SCRATCH_DIR)/nextufs-fuse-write.raw $(SCRATCH_DIR)/nextufs-fuse-write-mnt -o rw -f -s >$(SCRATCH_DIR)/nextufs_fuse_write.log 2>&1 & \
		pid=$$!; \
		trap 'kill $$pid 2>/dev/null || true; fusermount3 -u $(SCRATCH_DIR)/nextufs-fuse-write-mnt >/dev/null 2>&1 || true' EXIT INT TERM; \
		sleep 1; \
		printf 'hello' >$(SCRATCH_DIR)/nextufs-fuse-write-mnt/private/tmp/nextufs-fuse-write; \
		printf '+world' >>$(SCRATCH_DIR)/nextufs-fuse-write-mnt/private/tmp/nextufs-fuse-write; \
		mv $(SCRATCH_DIR)/nextufs-fuse-write-mnt/private/tmp/nextufs-fuse-write $(SCRATCH_DIR)/nextufs-fuse-write-mnt/private/tmp/nextufs-fuse-write-renamed; \
		ln $(SCRATCH_DIR)/nextufs-fuse-write-mnt/private/tmp/nextufs-fuse-write-renamed $(SCRATCH_DIR)/nextufs-fuse-write-mnt/private/tmp/nextufs-fuse-write-hard; \
		ln -s nextufs-fuse-write-renamed $(SCRATCH_DIR)/nextufs-fuse-write-mnt/private/tmp/nextufs-fuse-write-soft; \
		mkdir $(SCRATCH_DIR)/nextufs-fuse-write-mnt/private/tmp/nextufs-fuse-dir; \
		truncate -s 14000 $(SCRATCH_DIR)/nextufs-fuse-write-mnt/private/tmp/nextufs-fuse-write-renamed; \
		printf 'tail' | dd of=$(SCRATCH_DIR)/nextufs-fuse-write-mnt/private/tmp/nextufs-fuse-write-renamed bs=1 seek=12288 conv=notrunc status=none; \
		mkfifo $(SCRATCH_DIR)/nextufs-fuse-write-mnt/private/tmp/nextufs-fuse-fifo; \
		test -p $(SCRATCH_DIR)/nextufs-fuse-write-mnt/private/tmp/nextufs-fuse-fifo; \
		test -d $(SCRATCH_DIR)/nextufs-fuse-write-mnt/private/tmp/nextufs-fuse-dir; \
		test "$$(readlink $(SCRATCH_DIR)/nextufs-fuse-write-mnt/private/tmp/nextufs-fuse-write-soft)" = "nextufs-fuse-write-renamed"; \
		dd if=$(SCRATCH_DIR)/nextufs-fuse-write-mnt/private/tmp/nextufs-fuse-write-renamed of=$(SCRATCH_DIR)/nextufs-fuse-tail-actual.bin bs=1 skip=12288 count=4 status=none; \
		cmp $(SCRATCH_DIR)/nextufs-fuse-tail.bin $(SCRATCH_DIR)/nextufs-fuse-tail-actual.bin; \
		rm $(SCRATCH_DIR)/nextufs-fuse-write-mnt/private/tmp/nextufs-fuse-fifo; \
		rmdir $(SCRATCH_DIR)/nextufs-fuse-write-mnt/private/tmp/nextufs-fuse-dir; \
		fusermount3 -u $(SCRATCH_DIR)/nextufs-fuse-write-mnt; \
		wait $$pid || true; \
		trap - EXIT INT TERM
	./nextufs browse $(SCRATCH_DIR)/nextufs-fuse-write.raw /private/tmp/nextufs-fuse-write-renamed >$(SCRATCH_DIR)/nextufs_fuse_write_browse.out
	grep -F 'size=14000' $(SCRATCH_DIR)/nextufs_fuse_write_browse.out
	grep -F 'hello+world' $(SCRATCH_DIR)/nextufs_fuse_write_browse.out
	dd if=$(SCRATCH_DIR)/nextufs-fuse-write.raw of=$(SCRATCH_DIR)/nextufs-fuse-write-a.raw bs=1024 skip=160 count=2096480 status=none
	$(FSCK_BIN) -n $(SCRATCH_DIR)/nextufs-fuse-write-a.raw >$(SCRATCH_DIR)/nextufs_fuse_write_fsck.out
	grep -F '** Phase 5 - Check Cyl groups' $(SCRATCH_DIR)/nextufs_fuse_write_fsck.out

test-permissions: all
	cp --reflink=auto $(TEST_IMAGE) $(SCRATCH_DIR)/nextufs-permissions.raw
	./nextufs mkfile $(SCRATCH_DIR)/nextufs-permissions.raw /private/tmp/nextufs-perm-file 'perm data'
	./nextufs mkfile --chown $(SCRATCH_DIR)/nextufs-permissions.raw /private/tmp/nextufs-perm-file 1000 200
	./nextufs mkfile --chmod $(SCRATCH_DIR)/nextufs-permissions.raw /private/tmp/nextufs-perm-file 0644
	./nextufs mkfile --mkdir $(SCRATCH_DIR)/nextufs-permissions.raw /private/tmp/nextufs-sticky
	./nextufs mkfile --chmod $(SCRATCH_DIR)/nextufs-permissions.raw /private/tmp/nextufs-sticky 01777
	./nextufs mkfile $(SCRATCH_DIR)/nextufs-permissions.raw /private/tmp/nextufs-sticky/victim 'victim'
	./nextufs mkfile --chown $(SCRATCH_DIR)/nextufs-permissions.raw /private/tmp/nextufs-sticky/victim 1000 200
	if ./nextufs mkfile --policy user --uid 1001 --gid 200 --chmod $(SCRATCH_DIR)/nextufs-permissions.raw /private/tmp/nextufs-perm-file 0600; then exit 1; fi
	./nextufs mkfile --policy user --uid 1000 --gid 201 --chmod $(SCRATCH_DIR)/nextufs-permissions.raw /private/tmp/nextufs-perm-file 03755
	./nextufs mkfile --policy user --uid 1000 --gid 333 --chown $(SCRATCH_DIR)/nextufs-permissions.raw /private/tmp/nextufs-perm-file keep 333
	if ./nextufs mkfile --policy user --uid 1000 --gid 333 --chown $(SCRATCH_DIR)/nextufs-permissions.raw /private/tmp/nextufs-perm-file 1002 333; then exit 1; fi
	if ./nextufs mkfile --policy user --uid 1001 --gid 333 --utimes $(SCRATCH_DIR)/nextufs-permissions.raw /private/tmp/nextufs-perm-file 111111111 222222222; then exit 1; fi
	if ./nextufs mkfile --policy user --uid 1001 --gid 1001 --unlink $(SCRATCH_DIR)/nextufs-permissions.raw /private/tmp/nextufs-sticky/victim; then exit 1; fi
	if ./nextufs mkfile --policy user --uid 1001 --gid 1001 --rename $(SCRATCH_DIR)/nextufs-permissions.raw /private/tmp/nextufs-sticky/victim /private/tmp/nextufs-sticky/victim2; then exit 1; fi
	if ./nextufs mkfile --policy user --uid 40000 --gid 0 $(SCRATCH_DIR)/nextufs-permissions.raw /private/tmp/nextufs-overflow x; then exit 1; fi
	if ./nextufs mkfile --policy user --uid 0 --gid 0 --chown $(SCRATCH_DIR)/nextufs-permissions.raw /private/tmp/nextufs-perm-file 40000 0; then exit 1; fi
	./nextufs browse $(SCRATCH_DIR)/nextufs-permissions.raw /private/tmp/nextufs-perm-file >$(SCRATCH_DIR)/nextufs_permissions_browse.out
	grep -F 'mode=0100755' $(SCRATCH_DIR)/nextufs_permissions_browse.out
	grep -F 'uid=1000 gid=333' $(SCRATCH_DIR)/nextufs_permissions_browse.out
	./nextufs browse $(SCRATCH_DIR)/nextufs-permissions.raw /private/tmp/nextufs-sticky/victim >$(SCRATCH_DIR)/nextufs_permissions_victim_browse.out
	grep -F "lookup '/private/tmp/nextufs-sticky/victim':" $(SCRATCH_DIR)/nextufs_permissions_victim_browse.out
	./nextufs browse $(SCRATCH_DIR)/nextufs-permissions.raw /private/tmp/nextufs-overflow >$(SCRATCH_DIR)/nextufs_permissions_overflow_browse.out 2>&1
	grep -F "lookup '/private/tmp/nextufs-overflow' failed" $(SCRATCH_DIR)/nextufs_permissions_overflow_browse.out
	dd if=$(SCRATCH_DIR)/nextufs-permissions.raw of=$(SCRATCH_DIR)/nextufs-permissions-a.raw bs=1024 skip=160 count=2096480 status=none
	$(FSCK_BIN) -n $(SCRATCH_DIR)/nextufs-permissions-a.raw >$(SCRATCH_DIR)/nextufs_permissions_fsck.out
	grep -F '** Phase 5 - Check Cyl groups' $(SCRATCH_DIR)/nextufs_permissions_fsck.out

test-failure: all
	$(MKIMG_BIN) --force-overwrite --raw $(SCRATCH_DIR)/nextufs-failure.raw 4096 63 16 8192 1024 16 10 60 2048 t
	printf 'ok' >$(SCRATCH_DIR)/nextufs-seed.bin
	dd if=/dev/zero of=$(SCRATCH_DIR)/nextufs-too-big.bin bs=1000000 count=6 status=none
	./nextufs mkfile --from-file $(SCRATCH_DIR)/nextufs-failure.raw /seed $(SCRATCH_DIR)/nextufs-seed.bin
	if ./nextufs mkfile --from-file $(SCRATCH_DIR)/nextufs-failure.raw /too-big $(SCRATCH_DIR)/nextufs-too-big.bin; then exit 1; fi
	./nextufs browse $(SCRATCH_DIR)/nextufs-failure.raw /too-big >$(SCRATCH_DIR)/nextufs_failure_too_big_browse.out 2>&1
	grep -F "lookup '/too-big' failed" $(SCRATCH_DIR)/nextufs_failure_too_big_browse.out
	./nextufs browse $(SCRATCH_DIR)/nextufs-failure.raw /seed >$(SCRATCH_DIR)/nextufs_failure_seed_browse_before.out
	grep -F 'size=2' $(SCRATCH_DIR)/nextufs_failure_seed_browse_before.out
	grep -F 'ok' $(SCRATCH_DIR)/nextufs_failure_seed_browse_before.out
	if ./nextufs mkfile --truncate $(SCRATCH_DIR)/nextufs-failure.raw /seed 6000000; then exit 1; fi
	./nextufs browse $(SCRATCH_DIR)/nextufs-failure.raw /seed >$(SCRATCH_DIR)/nextufs_failure_seed_browse_after.out
	grep -F 'size=2' $(SCRATCH_DIR)/nextufs_failure_seed_browse_after.out
	grep -F 'ok' $(SCRATCH_DIR)/nextufs_failure_seed_browse_after.out
	$(FSCK_BIN) -n $(SCRATCH_DIR)/nextufs-failure.raw >$(SCRATCH_DIR)/nextufs_failure_fsck.out
	grep -F '** Phase 5 - Check Cyl groups' $(SCRATCH_DIR)/nextufs_failure_fsck.out

test-stress: all nextufs_stress
	$(MKIMG_BIN) --force-overwrite --raw $(SCRATCH_DIR)/nextufs-stress.raw 8192 63 16 8192 1024 16 10 60 2048 t
	./nextufs_stress --seed 0x13579bdf --ops 250 --quiet $(SCRATCH_DIR)/nextufs-stress.raw >$(SCRATCH_DIR)/nextufs_stress.log
	$(FSCK_BIN) -n $(SCRATCH_DIR)/nextufs-stress.raw >$(SCRATCH_DIR)/nextufs_stress_fsck.out
	grep -F 'nextufs_stress: ok' $(SCRATCH_DIR)/nextufs_stress.log
	grep -F '** Phase 5 - Check Cyl groups' $(SCRATCH_DIR)/nextufs_stress_fsck.out

test-stress-base: all nextufs_stress
	cp --reflink=auto $(TEST_IMAGE) $(SCRATCH_DIR)/nextufs-stress-base.raw
	./nextufs_stress --seed 0x2468ace0 --ops 220 --root /private/tmp/nextufs-stress --quiet $(SCRATCH_DIR)/nextufs-stress-base.raw >$(SCRATCH_DIR)/nextufs_stress_base.log
	dd if=$(SCRATCH_DIR)/nextufs-stress-base.raw of=$(SCRATCH_DIR)/nextufs-stress-base-a.raw bs=1024 skip=160 count=2096480 status=none
	$(FSCK_BIN) -n $(SCRATCH_DIR)/nextufs-stress-base-a.raw >$(SCRATCH_DIR)/nextufs_stress_base_fsck.out
	grep -F 'nextufs_stress: ok' $(SCRATCH_DIR)/nextufs_stress_base.log
	grep -F '** Phase 5 - Check Cyl groups' $(SCRATCH_DIR)/nextufs_stress_base_fsck.out

test-stress-batch: all nextufs_stress
	rm -rf $(SCRATCH_DIR)/nextufs-stress-batch-fail
	$(MKIMG_BIN) --force-overwrite --raw $(SCRATCH_DIR)/nextufs-stress-batch.raw 8192 63 16 8192 1024 16 10 60 2048 t
	./nextufs_stress --seed 0x13579bdf --ops 120 --batch 4 --save-fail-dir $(SCRATCH_DIR)/nextufs-stress-batch-fail --quiet $(SCRATCH_DIR)/nextufs-stress-batch.raw >$(SCRATCH_DIR)/nextufs_stress_batch.log
	grep -F 'nextufs_stress: batch ok' $(SCRATCH_DIR)/nextufs_stress_batch.log

test-stress-fuse: all nextufs_stress
	$(MKIMG_BIN) --force-overwrite --raw $(SCRATCH_DIR)/nextufs-stress-fuse.raw 8192 63 16 8192 1024 16 10 60 2048 t
	./nextufs_stress --backend fuse --seed 0x13579bdf --ops 120 --quiet $(SCRATCH_DIR)/nextufs-stress-fuse.raw >$(SCRATCH_DIR)/nextufs_stress_fuse.log
	$(FSCK_BIN) -n $(SCRATCH_DIR)/nextufs-stress-fuse.raw >$(SCRATCH_DIR)/nextufs_stress_fuse_fsck.out
	grep -F 'nextufs_stress: ok' $(SCRATCH_DIR)/nextufs_stress_fuse.log
	grep -F '** Phase 5 - Check Cyl groups' $(SCRATCH_DIR)/nextufs_stress_fuse_fsck.out

repair-tools: tests/fsck/tools/corrupt_raw_case

tests/fsck/tools/corrupt_raw_case: tests/fsck/tools/corrupt_raw_case.c $(PUBLIC_HDRS) $(LIB) $(WRITE_LIB)
	$(CC) $(CFLAGS) -Isrc/fsck/include -o $@ $< $(WRITE_LIB) $(LIB)

repair-corpus: all repair-tools
	tests/fsck/scripts/build_corpus.sh

repair-lab: repair-corpus

repair-smoke: all repair-tools
	tests/fsck/scripts/run_case.sh shipped -n bad-block-count >/dev/null
	tests/fsck/scripts/run_case.sh shipped -y bad-block-count >/dev/null

repair-repair-all: all repair-tools
	tests/fsck/scripts/repair_all_cases.sh

install: nextufs
	$(INSTALL) -d "$(DESTDIR)$(bindir)"
	$(INSTALL) -m 0755 nextufs "$(DESTDIR)$(bindir)/nextufs"
	$(INSTALL) -d "$(DESTDIR)$(mandir)/man1"
	$(INSTALL) -m 0644 man/nextufs.1 "$(DESTDIR)$(mandir)/man1/nextufs.1"

uninstall:
	rm -f "$(DESTDIR)$(bindir)/nextufs"
	rm -f "$(DESTDIR)$(mandir)/man1/nextufs.1"

clean:
	rm -rf $(BUILD_DIR)
	rm -f nextufs nextufs_test nextufs_stress \
		tests/fsck/tools/corrupt_raw_case
	rm -f *.o *.a src/commands/*.o src/core/*.o src/mutate/*.o \
		tests/nextufs/*.o
