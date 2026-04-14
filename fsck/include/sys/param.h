#ifndef PORTS_FSCK_SYS_PARAM_H
#define PORTS_FSCK_SYS_PARAM_H

#include <sys/types.h>
#include <sys/time.h>
#include <signal.h>
#include <stddef.h>

#define BSD 43
#define BSD4_3 1

#ifndef MAXBSIZE
#define MAXBSIZE 8192
#endif
#define DEV_BSIZE 1024
#define DEV_BSHIFT 10
#define BLKDEV_IOSIZE 2048
#define MAXFRAG 8
#define MAXPATHLEN 1024
#define MAXSYMLINKS 8
#define MAXHOSTNAMELEN 256
#define MAXNAMLEN 255
#define NBBY 8

#define btodb(bytes) ((unsigned)(bytes) >> DEV_BSHIFT)
#define dbtob(db) ((unsigned)(db) << DEV_BSHIFT)

#define setbit(a,i) (*(((unsigned char *)(a)) + ((i) / NBBY)) |= 1 << ((i) % NBBY))
#define clrbit(a,i) (*(((unsigned char *)(a)) + ((i) / NBBY)) &= ~(1 << ((i) % NBBY)))
#define isset(a,i) (*(((unsigned char *)(a)) + ((i) / NBBY)) & (1 << ((i) % NBBY)))
#define isclr(a,i) ((((unsigned char *)(a))[(i) / NBBY] & (1 << ((i) % NBBY))) == 0)

#define howmany(x, y) (((x) + ((y) - 1)) / (y))
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#define powerof2(x) ((((x) - 1) & (x)) == 0)

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#ifndef quad
typedef struct {
	int val[2];
} quad;
#endif

#endif
