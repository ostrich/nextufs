#include "nextufs_size.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

int
nextufs_parse_u64(const char *s, uint64_t *out)
{
	char *end;
	unsigned long long v;

	errno = 0;
	v = strtoull(s, &end, 10);
	if (errno != 0 || end == s || *end != '\0')
		return -1;
	*out = (uint64_t)v;
	return 0;
}

int
nextufs_parse_size_bytes(const char *s,
    enum nextufs_bare_size_unit bare_unit, uint64_t *bytes_out)
{
	char *end;
	unsigned long long value;
	uint64_t mult = 1;

	errno = 0;
	value = strtoull(s, &end, 10);
	if (errno != 0 || end == s)
		return -1;
	if (*end == '\0') {
		mult = bare_unit == NEXTUFS_BARE_SIZE_1K_SECTORS ?
		    NEXTUFS_KIB_BYTES : 1;
	} else {
		if (end[1] != '\0')
			return -1;
		switch (*end) {
		case 'k':
		case 'K':
			mult = NEXTUFS_KIB_BYTES;
			break;
		case 'm':
		case 'M':
			mult = NEXTUFS_KIB_BYTES * NEXTUFS_KIB_BYTES;
			break;
		case 'g':
		case 'G':
			mult = NEXTUFS_KIB_BYTES * NEXTUFS_KIB_BYTES *
			    NEXTUFS_KIB_BYTES;
			break;
		default:
			return -1;
		}
	}
	if (value == 0 || (uint64_t)value > UINT64_MAX / mult)
		return -1;
	*bytes_out = (uint64_t)value * mult;
	return 0;
}
