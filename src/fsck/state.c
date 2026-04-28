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

static struct fsck_ctx *g_fsck_current_ctx;
static volatile sig_atomic_t g_fsck_return_to_single_user;

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
fsck_driver_reset_signal_state(void)
{
	g_fsck_return_to_single_user = 0;
}

void
fsck_driver_request_single_user_return(void)
{
	g_fsck_return_to_single_user = 1;
}

int
fsck_driver_should_return_to_single_user(void)
{
	return g_fsck_return_to_single_user != 0;
}

void
fsck_ctx_init(struct fsck_ctx *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->ctx_dfile.rfdes = -1;
	ctx->ctx_dfile.wfdes = -1;
	ctx->ctx_dfile.image.fd = -1;
	ctx->ctx_lfname = "lost+found";
	ctx->ctx_source_path[0] = '\0';
	ctx->ctx_endpathname = &ctx->ctx_pathname[BUFSIZ - 2];
	ctx->ctx_emptydir = (struct dirtemplate){ 0, DIRBLKSIZ, 0, "", 0, 0, 0, "" };
	ctx->ctx_dirhead = (struct dirtemplate){
		0, 12, 1, ".", 0, DIRBLKSIZ - 12, 2, ".."
	};
}

void
fsck_ctx_init_from_runtime(struct fsck_ctx *ctx,
    const struct fsck_runtime_options *opts)
{
	fsck_ctx_init(ctx);
#if	NeXT
	ctx->ctx_Pflag = opts->opt_Pflag;
#endif
	ctx->ctx_nflag = opts->opt_nflag;
	ctx->ctx_yflag = opts->opt_yflag;
	ctx->ctx_bflag = opts->opt_bflag;
	ctx->ctx_debug = opts->opt_debug;
	ctx->ctx_preen = opts->opt_preen;
}
