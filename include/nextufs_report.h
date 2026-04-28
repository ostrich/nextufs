#ifndef NEXTUFS_REPORT_H
#define NEXTUFS_REPORT_H

#include "nextufs_info.h"

#include <stdio.h>
#include <stdint.h>

void nextufs_report_error(FILE *out, const char *command, const char *message);
void nextufs_report_invalid_value(FILE *out, const char *command,
	const char *field, const char *value);
void nextufs_report_errno(FILE *out, const char *command, const char *action,
	const char *subject, int rc);
void nextufs_report_info_text(FILE *out, const char *source,
	const struct nextufs_info *info);
void nextufs_report_info_json(FILE *out, const char *source,
	const struct nextufs_info *info);

#endif
