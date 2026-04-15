/* Per-run checker state and process-level runtime options. */

#include <string.h>
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

struct fsck_runtime_options fsck_runtime_opts;
int fsck_process_exitstat;

static struct fsck_ctx *g_fsck_current_ctx;

struct fsck_ctx *
fsck_ctx_current(void)
{
	return g_fsck_current_ctx;
}

void
fsck_ctx_set_current(struct fsck_ctx *ctx)
{
	g_fsck_current_ctx = ctx;
}

void
fsck_ctx_init(struct fsck_ctx *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->ctx_lfname = "lost+found";
	ctx->ctx_endpathname = &ctx->ctx_pathname[BUFSIZ - 2];
	ctx->ctx_emptydir = (struct dirtemplate){ 0, DIRBLKSIZ, 0, "", 0, 0, 0, "" };
	ctx->ctx_dirhead = (struct dirtemplate){
		0, 12, 1, ".", 0, DIRBLKSIZ - 12, 2, ".."
	};
}

void
fsck_ctx_init_from_runtime(struct fsck_ctx *ctx)
{
	fsck_ctx_init(ctx);
#if	NeXT
	ctx->ctx_Pflag = fsck_runtime_opts.opt_Pflag;
#endif
	ctx->ctx_nflag = fsck_runtime_opts.opt_nflag;
	ctx->ctx_yflag = fsck_runtime_opts.opt_yflag;
	ctx->ctx_bflag = fsck_runtime_opts.opt_bflag;
	ctx->ctx_debug = fsck_runtime_opts.opt_debug;
	ctx->ctx_preen = fsck_runtime_opts.opt_preen;
}
