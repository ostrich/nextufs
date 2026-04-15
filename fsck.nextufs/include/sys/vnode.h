#ifndef PORTS_FSCK_SYS_VNODE_H
#define PORTS_FSCK_SYS_VNODE_H

#include <sys/types.h>

enum vtype { VNON, VREG, VDIR, VBLK, VCHR, VLNK, VSOCK, VBAD, VFIFO, VSTR };

struct vnode {
	unsigned short v_flag;
	unsigned short v_count;
	enum vtype v_type;
	dev_t v_rdev;
	void *v_data;
};

#endif
