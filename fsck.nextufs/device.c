/*
 * Equivalence note:
 * This file groups low-level device access and mounted-filesystem discovery.
 * The logic is moved, not changed: read/write retry behavior, prompting, and
 * mounted-state detection remain the legacy behavior.
 */

#include <stdio.h>
#include <sys/param.h>
#include <ufs/fs.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <ufs/inode.h>
#if	NeXT
#include <sys/file.h>
#include <sys/errno.h>
#endif
#define KERNEL
#include <ufs/fsdir.h>
#undef KERNEL
#include <mntent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "fsck.h"

void
rwerr(char *s, daddr_t blk)
{
	if (preen == 0)
		printf("\n");
	pfatal("CANNOT %s: BLK %ld", s, blk);
	if (reply("CONTINUE") == 0)
		errexit("Program terminated\n");
}

int
bread(struct filecntl *fcp, char *buf, daddr_t blk, long size)
{
	char *cp;
	int i, errs;

	if (lseek(fcp->rfdes, (long)dbtob(blk), 0) < 0)
		rwerr("SEEK", blk);
	else if (read(fcp->rfdes, buf, (int)size) == size)
		return (0);
	rwerr("READ", blk);
	if (lseek(fcp->rfdes, (long)dbtob(blk), 0) < 0)
		rwerr("SEEK", blk);
	errs = 0;
	pfatal("THE FOLLOWING SECTORS COULD NOT BE READ:");
	for (cp = buf, i = 0; i < size; i += DEV_BSIZE, cp += DEV_BSIZE) {
		if (read(fcp->rfdes, cp, DEV_BSIZE) < 0) {
			printf(" %ld,", (long)(blk + i / DEV_BSIZE));
			bzero(cp, DEV_BSIZE);
			errs++;
		}
	}
	printf("\n");
	return (errs);
}

void
bwrite(struct filecntl *fcp, char *buf, daddr_t blk, int size)
{
	int i;
	char *cp;

	if (fcp->wfdes < 0)
		return;
	if (lseek(fcp->wfdes, (long)dbtob(blk), 0) < 0)
		rwerr("SEEK", blk);
	else if (write(fcp->wfdes, buf, (int)size) == size) {
		fcp->mod = 1;
		return;
	}
	rwerr("WRITE", blk);
	if (lseek(fcp->wfdes, (long)dbtob(blk), 0) < 0)
		rwerr("SEEK", blk);
	pfatal("THE FOLLOWING SECTORS COULD NOT BE WRITTEN:");
	for (cp = buf, i = 0; i < size; i += DEV_BSIZE, cp += DEV_BSIZE)
		if (write(fcp->wfdes, cp, DEV_BSIZE) < 0)
			printf(" %ld,", (long)(blk + i / DEV_BSIZE));
	printf("\n");
}

int
mounted(char *name)
{
	int found = 0;
	struct mntent *mnt;
	FILE *mnttab;
	char *blkname;
	static char buf[MAXPATHLEN];

	(void) strcpy(buf, name);
	blkname = unrawname(buf);

#if	NeXT
	mountedfs = rootfs = readonlyfs = 0;
#endif
	if (is_mounted_on("/", blkname)) {
#if	NeXT
		mountedfs = rootfs = 1;
		readonlyfs = (access("/", W_OK) < 0 && errno == EROFS);
#endif
		return (1);
	}

	mnttab = setmntent(MOUNTED, "r");
	if (mnttab == NULL) {
		printf("can't open %s\n", MOUNTED);
		return (0);
	}
	while (!found && (mnt = getmntent(mnttab)) != NULL) {
		if (strcmp(mnt->mnt_type, MNTTYPE_43) != 0)
			continue;
		if (strcmp(blkname, mnt->mnt_fsname) != 0)
			continue;
		found = is_mounted_on(mnt->mnt_dir, mnt->mnt_fsname);
	}
	endmntent(mnttab);
#if	NeXT
	mountedfs = found;
	if (found)
		readonlyfs = (access(mnt->mnt_dir, W_OK) < 0 && errno == EROFS);
#endif
	return (found);
}

int
is_mounted_on(char *dir, char *dev)
{
	struct stat device_stat, mount_stat;

	if (stat(dir, &mount_stat) < 0)
		return (0);
	if (stat(dev, &device_stat) < 0)
		return (0);
	return (device_stat.st_rdev == mount_stat.st_dev);
}

void *
xmalloc(unsigned long size)
{
	void *ret;

	if ((ret = (char *)malloc(size)) == NULL)
		errexit("ran out of memory!\n");
	return (ret);
}

struct mntent *
mntdup(struct mntent *mnt)
{
	struct mntent *new;

	new = (struct mntent *)xmalloc(sizeof(*new));

	new->mnt_fsname = (char *)xmalloc(strlen(mnt->mnt_fsname) + 1);
	strcpy(new->mnt_fsname, mnt->mnt_fsname);

	new->mnt_dir = (char *)xmalloc(strlen(mnt->mnt_dir) + 1);
	strcpy(new->mnt_dir, mnt->mnt_dir);

	new->mnt_type = (char *)xmalloc(strlen(mnt->mnt_type) + 1);
	strcpy(new->mnt_type, mnt->mnt_type);

	new->mnt_opts = (char *)xmalloc(strlen(mnt->mnt_opts) + 1);
	strcpy(new->mnt_opts, mnt->mnt_opts);

	new->mnt_freq = mnt->mnt_freq;
	new->mnt_passno = mnt->mnt_passno;
	return (new);
}
