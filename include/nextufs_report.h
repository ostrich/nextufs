#ifndef NEXTUFS_REPORT_H
#define NEXTUFS_REPORT_H

#include "nextufs_info.h"

#include <stdio.h>
#include <stdint.h>

void nextufs_report_size_line(FILE *out, const char *label, uint64_t bytes);
void nextufs_report_info_text(FILE *out, const char *source,
	const struct nextufs_info *info);
void nextufs_report_info_json(FILE *out, const char *source,
	const struct nextufs_info *info);

#endif
