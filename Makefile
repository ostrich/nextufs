.DEFAULT_GOAL := all
SHELL := /bin/bash
.SHELLFLAGS := -eu -o pipefail -c
CC = cc
prefix ?= /usr/local
bindir ?= $(prefix)/bin
mandir ?= $(prefix)/share/man
DESTDIR ?=
INSTALL ?= install
CFLAGS = -Iinclude -Isrc -O2 -g -std=gnu99 -Wall -Wextra -Werror
FORMAT_CFLAGS ?= -O2 -g -std=gnu89 -Wall -Wextra
FORMAT_CPPFLAGS = -DNeXT -DNeXT_MOD -DNeXT_NFS -Isrc/mkimg_format/include -Iinclude \
	-Disblock=mkimg_isblock -Dclrblock=mkimg_clrblock \
	-Dsetblock=mkimg_setblock -Dswap_superblock=mkimg_swap_superblock
FSCK_CFLAGS ?= -O2 -g -std=gnu99 -fcommon -Wall -Wextra
FSCK_CPPFLAGS = -DNeXT=1 -DNeXT_MOD=1 -DFASTLINK=1 \
	-Isrc/fsck/include -Iinclude
BUILD_DIR = build
WITH_FUSE ?= 1
ifeq ($(filter $(WITH_FUSE),0 1),)
$(error WITH_FUSE must be 0 or 1)
endif
OBJ_DIR = $(BUILD_DIR)/obj/fuse-$(WITH_FUSE)
TEST_IMAGE ?=
NEXTUFS_BIN = $(BUILD_DIR)/bin/fuse-$(WITH_FUSE)/nextufs
TEST_BIN_DIR = $(BUILD_DIR)/tests/fuse-$(WITH_FUSE)
API_TEST = $(TEST_BIN_DIR)/api
STRESS_TEST = $(TEST_BIN_DIR)/stress
CORRUPT_TEST = $(TEST_BIN_DIR)/corrupt
QCOW2_PROBE = $(TEST_BIN_DIR)/qcow2-probe
QCOW2_FAULT_BIN = $(TEST_BIN_DIR)/qcow2-fault
QCOW2_WRAPS = -Wl,--wrap=pwrite,--wrap=pread,--wrap=fsync,--wrap=ftruncate
ifeq ($(WITH_FUSE),1)
FUSE_CFLAGS := $(shell pkg-config --cflags fuse3)
FUSE_LIBS := $(shell pkg-config --libs fuse3)
MOUNT_SRC = src/commands/mount.c
else
FUSE_CFLAGS =
FUSE_LIBS =
MOUNT_SRC = src/commands/mount_stub.c
endif
LIB_SRCS = src/core/image.c src/core/directory.c src/core/path.c src/core/node.c \
	src/core/layout.c src/core/alloc.c src/core/label.c src/core/size.c \
	src/core/source.c src/core/transaction.c src/core/info.c src/core/report.c src/core/qcow2.c
LIB_OBJS = $(LIB_SRCS:%.c=$(OBJ_DIR)/%.o)
LIB = $(OBJ_DIR)/libnextufs.a
WRITE_SRCS = src/mutate/dir_mutate.c src/mutate/mutate.c src/mutate/file_mutate.c
WRITE_OBJS = $(WRITE_SRCS:%.c=$(OBJ_DIR)/%.o)
WRITE_LIB = $(OBJ_DIR)/libnextufs_mutate.a
COMMAND_SRCS = src/commands/main.c $(MOUNT_SRC) \
	src/commands/info.c src/commands/browse.c src/commands/fsck.c \
	src/commands/mkfile.c src/commands/mkimg.c src/commands/resize.c
COMMAND_OBJS = $(COMMAND_SRCS:%.c=$(OBJ_DIR)/%.o)
STRESS_OBJ = $(OBJ_DIR)/tests/stress/stress.o
TEST_OBJ = $(OBJ_DIR)/tests/api/api_test.o
FORMAT_OBJS = $(OBJ_DIR)/src/mkimg_format/format.o \
	$(OBJ_DIR)/src/mkimg_format/format_fsinit.o \
	$(OBJ_DIR)/src/mkimg_format/format_io.o
FSCK_SRCS = alloc_map.c buffer.c byteorder.c device.c dir_repair.c \
	dir_scan.c driver.c frag_support.c inode_ops.c inode_scan.c operator.c \
	pass1.c pass1b.c pass2.c pass3.c pass4.c pass5.c session.c setup.c \
	source.c state.c
FSCK_OBJS = $(FSCK_SRCS:%.c=$(OBJ_DIR)/src/fsck/%.o)
FSCK_HDRS = include/nextufs_ufs_types.h $(wildcard src/fsck/include/*.h src/fsck/include/sys/*.h src/fsck/include/ufs/*.h)
FORMAT_HDRS = include/nextufs_ufs_types.h $(wildcard src/mkimg_format/include/sys/*.h src/mkimg_format/include/ufs/*.h)
PUBLIC_HDRS = include/nextufs_image.h include/nextufs_node.h \
	include/nextufs_mutate.h include/nextufs_info.h include/nextufs_label.h \
	include/nextufs_report.h include/nextufs_size.h
INTERNAL_HDRS = $(PUBLIC_HDRS) include/nextufs_internal.h

.PHONY: all clean install uninstall test-tools FORCE
FORCE:

all: nextufs

$(LIB): $(LIB_OBJS)
	@mkdir -p $(@D)
	ar rcs $@ $(LIB_OBJS)

$(WRITE_LIB): $(WRITE_OBJS)
	@mkdir -p $(@D)
	ar rcs $@ $(WRITE_OBJS)

nextufs: $(NEXTUFS_BIN) FORCE
	ln -sfn "$(NEXTUFS_BIN)" $@

$(NEXTUFS_BIN): $(COMMAND_OBJS) $(FORMAT_OBJS) $(FSCK_OBJS) $(LIB) $(WRITE_LIB)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(FUSE_CFLAGS) -o $@ $(COMMAND_OBJS) \
		$(FORMAT_OBJS) $(FSCK_OBJS) $(WRITE_LIB) $(LIB) $(FUSE_LIBS)

$(API_TEST): $(TEST_OBJ) $(WRITE_LIB) $(LIB)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJ) $(WRITE_LIB) $(LIB)

$(STRESS_TEST): $(STRESS_OBJ) $(LIB) $(WRITE_LIB)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $(STRESS_OBJ) $(WRITE_LIB) $(LIB)

$(LIB_OBJS): $(INTERNAL_HDRS)
$(WRITE_OBJS): $(INTERNAL_HDRS)
$(COMMAND_OBJS): $(PUBLIC_HDRS) src/commands/commands.h
$(TEST_OBJ): tests/api/api_test.c $(PUBLIC_HDRS)
$(STRESS_OBJ): tests/stress/stress.c $(PUBLIC_HDRS)

$(OBJ_DIR)/src/commands/mount.o: src/commands/mount.c $(INTERNAL_HDRS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(FUSE_CFLAGS) -c -o $@ $<

$(OBJ_DIR)/src/commands/fsck.o: include/nextufs_fsck.h
$(OBJ_DIR)/src/commands/mkimg.o: src/commands/mkimg.c $(PUBLIC_HDRS) src/mkimg_format/format.h $(FORMAT_HDRS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(FORMAT_CPPFLAGS) \
		-Isrc/mkimg_format -Iinclude -c -o $@ $<

$(OBJ_DIR)/src/commands/resize.o: $(INTERNAL_HDRS)
$(OBJ_DIR)/src/commands/%.o: src/commands/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(FORMAT_OBJS): $(OBJ_DIR)/src/mkimg_format/%.o: src/mkimg_format/%.c src/mkimg_format/format.h $(FORMAT_HDRS)
	@mkdir -p $(@D)
	$(CC) $(FORMAT_CPPFLAGS) $(FORMAT_CFLAGS) -Isrc/mkimg_format -c -o $@ $<

$(OBJ_DIR)/src/fsck/%.o: src/fsck/%.c src/fsck/fsck.h $(FSCK_HDRS) include/nextufs_image.h include/nextufs_fsck.h
	@mkdir -p $(@D)
	$(CC) $(FSCK_CPPFLAGS) $(FSCK_CFLAGS) -c -o $@ $<

$(OBJ_DIR)/%.o: %.c $(INTERNAL_HDRS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(CORRUPT_TEST): tests/fsck/tools/corrupt_raw_case.c $(PUBLIC_HDRS) $(FSCK_HDRS) $(LIB) $(WRITE_LIB)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -Isrc/fsck/include -o $@ $< $(WRITE_LIB) $(LIB)

$(QCOW2_PROBE): tests/qcow2/backend_probe.c tests/qcow2/io_faults.c $(INTERNAL_HDRS) $(LIB)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ tests/qcow2/backend_probe.c tests/qcow2/io_faults.c $(LIB) $(QCOW2_WRAPS)

$(QCOW2_FAULT_BIN): tests/qcow2/io_faults.c $(COMMAND_OBJS) $(FORMAT_OBJS) $(FSCK_OBJS) $(LIB) $(WRITE_LIB)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(FUSE_CFLAGS) -o $@ tests/qcow2/io_faults.c $(COMMAND_OBJS) \
		$(FORMAT_OBJS) $(FSCK_OBJS) $(WRITE_LIB) $(LIB) $(FUSE_LIBS) $(QCOW2_WRAPS)

test-tools: all $(API_TEST) $(STRESS_TEST) $(CORRUPT_TEST) $(QCOW2_PROBE) $(QCOW2_FAULT_BIN)

TEST_GROUPS = test test-fsck test-qcow2 test-fuse test-stress test-image test-all
.PHONY: $(TEST_GROUPS) test-list test-qcow2-sanitize test-qcow2-coverage
TEST_ARGS ?=
TEST_RUNNER = python3 -B tests/run.py --nextufs "$(NEXTUFS_BIN)" --bin-dir "$(TEST_BIN_DIR)" $(TEST_ARGS)

test: all $(API_TEST) $(CORRUPT_TEST)
test-fsck: all $(CORRUPT_TEST)
test test-fsck:
	$(TEST_RUNNER) $(if $(filter test,$@),default,fsck)

test-qcow2: all $(CORRUPT_TEST) $(QCOW2_PROBE) $(QCOW2_FAULT_BIN)
	$(TEST_RUNNER) qcow2

test-fuse test-stress: all $(STRESS_TEST)
test-image: all $(API_TEST) $(STRESS_TEST)
test-fuse test-stress test-image:
	$(TEST_RUNNER) $(patsubst test-%,%,$@) $(if $(TEST_IMAGE),--image "$(TEST_IMAGE)")

test-all: test-tools
	$(TEST_RUNNER) all

test-list:
	$(TEST_RUNNER) --list $(if $(TEST_GROUP),$(TEST_GROUP),all)

test-qcow2-sanitize test-qcow2-coverage: test-tools
	python3 -B tests/instrument.py $(patsubst test-qcow2-%,%,$@) --bin-dir "$(TEST_BIN_DIR)" --nextufs "$(NEXTUFS_BIN)"

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
	rm -f nextufs
