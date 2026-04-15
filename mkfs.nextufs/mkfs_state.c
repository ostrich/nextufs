#include "mkfs.h"

union mkfs_superblock_store fsun;
union mkfs_cg_store cgun;
struct csum *fscs;
struct dinode zino[MAXIPG];
char *fsys;
time_t utime;
int fsi;
int fso;
int Nflag;
