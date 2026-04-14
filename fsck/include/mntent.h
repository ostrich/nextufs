#ifndef PORTS_FSCK_MNTENT_H
#define PORTS_FSCK_MNTENT_H

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

#endif
