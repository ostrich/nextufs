#include "format.h"

union format_superblock_store fsun;
union format_cg_store cgun;
struct csum *fscs;
struct dinode zino[MAXIPG];
char *fsys;
time_t utime;
int fsi;
int fso;
int Nflag;
int format_no_create;
off_t format_base_offset;
