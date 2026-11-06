/* Source selection helpers for raw files, disk images, and VDI containers. */

#include <sys/param.h>
#include <ufs/fs.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <ufs/inode.h>
#define KERNEL
#include <ufs/fsdir.h>
#undef KERNEL
#include <mntent.h>
#include <sys/stat.h>

#include "fsck.h"

int
fsck_source_use_image_backend(const char *path)
{
	struct stat st;

	if (stat(path, &st) < 0)
		return (0);
	return ((st.st_mode & S_IFMT) == S_IFREG);
}

void
fsck_source_cleanup(void)
{
	unsigned i;
	fsck_file_close(&dfile);
	free(blockmap);
	blockmap = NULL;
	free(statemap);
	statemap = NULL;
	free(lncntp);
	lncntp = NULL;
	for (i = 0; i < MAXCSBUFS; i++) {
		free(fsck_fs_csp[i]);
		fsck_fs_csp[i] = NULL;
	}
	while (duplist) {
		struct dups *next = duplist->next;
		free(duplist);
		duplist = next;
	}
	while (zlnhead) {
		struct zlncnt *next = zlnhead->next;
		free(zlnhead);
		zlnhead = next;
	}
}
