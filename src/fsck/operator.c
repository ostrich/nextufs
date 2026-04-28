/* Operator interaction, prompts, and process-level reporting. */

#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>
#include <sys/param.h>
#include <ufs/fs.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <ufs/inode.h>
#define KERNEL
#include <ufs/fsdir.h>
#undef KERNEL
#include <mntent.h>
#include "fsck.h"

#define getline fsck_getline

int
reply(char *s)
{
	char line[80];

	if (preen)
		pfatal("INTERNAL ERROR: GOT TO reply()");
	printf("\n%s? ", s);
	if (nflag || !fsck_file_is_writable(&dfile)) {
		printf(" no\n\n");
		exitstat = 8;
		return (0);
	}
	if (yflag) {
		printf(" yes\n\n");
		return (1);
	}
	if (getline(stdin, line, sizeof(line)) == EOF)
		errexit("\n");
	printf("\n");
	if (line[0] == 'y' || line[0] == 'Y')
		return (1);
	exitstat = 8;
	return (0);
}

int
getline(FILE *fp, char *loc, int maxlen)
{
	register int n;
	register char *p, *lastloc;

	p = loc;
	lastloc = &p[maxlen - 1];
	while ((n = getc(fp)) != '\n') {
		if (n == EOF)
			return (EOF);
		if (!isspace(n) && p < lastloc)
			*p++ = n;
	}
	*p = 0;
	return (p - loc);
}

void
catch(int signo)
{
	(void)signo;

	ckfini();
	exit(12);
}

void
catchquit(int signo)
{
	extern int returntosingle;
	(void)signo;

	printf("returning to single-user after filesystem check\n");
	returntosingle = 1;
	(void)signal(SIGQUIT, SIG_DFL);
}

void
voidquit(int signo)
{
	(void)signo;

	sleep(1);
	(void)signal(SIGQUIT, SIG_IGN);
	(void)signal(SIGQUIT, SIG_DFL);
}

int
dofix(struct inodesc *idesc, char *msg)
{
	switch (idesc->id_fix) {
	case DONTKNOW:
		if (idesc->id_type == DATA)
			direrr(idesc->id_number, msg);
		else
			pwarn(msg);
		if (preen) {
			printf(" (SALVAGED)\n");
			idesc->id_fix = FIX;
			return (ALTERED);
		}
		if (reply("SALVAGE") == 0) {
			idesc->id_fix = NOFIX;
			return (0);
		}
		idesc->id_fix = FIX;
		return (ALTERED);

	case FIX:
		return (ALTERED);

	case NOFIX:
		return (0);

	default:
		errexit("UNKNOWN INODESC FIX MODE %d\n", idesc->id_fix);
	}
	return (0);
}

static void
vreport(char *prefix, char *fmt, va_list ap)
{
	if (prefix != 0)
		printf("%s", prefix);
	vprintf(fmt, ap);
}

void
errexit(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vreport(0, fmt, ap);
	va_end(ap);
	exit(8);
}

void
pfatal(char *fmt, ...)
{
	va_list ap;

#if	NeXT
	error_count++;
#endif
	if (preen) {
		printf("%s: ", devname);
		va_start(ap, fmt);
		vprintf(fmt, ap);
		va_end(ap);
		printf("\n");
		printf("%s: UNEXPECTED INCONSISTENCY; RUN fsck MANUALLY.\n",
			devname);
		exit(8);
	}
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

void
pwarn(char *fmt, ...)
{
	va_list ap;

#if	NeXT
	error_count++;
#endif
	if (preen)
		printf("%s: ", devname);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

#if	NeXT
void
pinfo(char *fmt, ...)
{
	va_list ap;

	if (preen)
		printf("%s: ", devname);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}
#endif

#ifndef lint
void
panic(const char *s)
{
	pfatal("INTERNAL INCONSISTENCY:");
	errexit((char *)s);
}
#endif
