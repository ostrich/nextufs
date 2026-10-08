#ifndef PORTS_FSCK_MNTENT_H
#define PORTS_FSCK_MNTENT_H

/* Image checking needs no host mount API. Keep legacy device/fstab support
 * on Linux; other POSIX hosts deliberately accept only image files. */
#ifndef NEXTUFS_FSCK_HOST_MOUNTS
#ifdef __linux__
#define NEXTUFS_FSCK_HOST_MOUNTS 1
#else
#define NEXTUFS_FSCK_HOST_MOUNTS 0
#endif
#endif

#if NEXTUFS_FSCK_HOST_MOUNTS
#include_next <mntent.h>

#ifndef MNTTAB
#define MNTTAB "/etc/fstab"
#endif

#ifndef MOUNTED
#define MOUNTED "/etc/mtab"
#endif

#ifndef MNTTYPE_43
#define MNTTYPE_43 "ufs"
#endif

#ifndef MNTOPT_QUOTA
#define MNTOPT_QUOTA "quota"
#endif

#endif /* NEXTUFS_FSCK_HOST_MOUNTS */
#endif
