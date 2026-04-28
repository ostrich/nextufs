#ifndef NEXTUFS_SIZE_H
#define NEXTUFS_SIZE_H

#include <stdint.h>

#define NEXTUFS_KIB_BYTES UINT64_C(1024)
#define NEXTUFS_COMPAT_MAX_BYTES UINT64_C(4294836224)
#define NEXTUFS_COMPAT_MAX_1K_SECTORS \
	(NEXTUFS_COMPAT_MAX_BYTES / NEXTUFS_KIB_BYTES)

enum nextufs_bare_size_unit {
	NEXTUFS_BARE_SIZE_BYTES = 0,
	NEXTUFS_BARE_SIZE_1K_SECTORS = 1,
};

int nextufs_parse_u64(const char *s, uint64_t *out);
int nextufs_parse_size_bytes(const char *s,
	enum nextufs_bare_size_unit bare_unit, uint64_t *bytes_out);

#endif
