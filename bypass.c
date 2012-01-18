/*
 * Copyright (c) 2011 Red Hat <http://www.redhat.com>
 */

#include <ctype.h>
#include <sys/uio.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "call-stub.h"
#include "defaults.h"
#include "logging.h"
#include "xlator.h"

#include "bypass.h"

int32_t
bypass_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
              off_t offset)
{
        bypass_private_t *priv = this->private;

        STACK_WIND (frame, default_readv_cbk, priv->target,
                    priv->target->fops->readv, fd, size, offset);
        return 0;
}


dict_t *
get_pending_dict (xlator_t *this)
{
	dict_t           *dict = NULL;
	xlator_list_t    *trav = NULL;
	char             *key = NULL;
	int32_t          *value = NULL;
        xlator_t         *afr = NULL;
        bypass_private_t *priv = this->private;

	dict = dict_new();
	if (!dict) {
		gf_log (this->name, GF_LOG_WARNING, "failed to allocate dict");
                return NULL;
	}

        afr = this->children->xlator;
	for (trav = afr->children; trav; trav = trav->next) {
                if (trav->xlator == priv->target) {
                        continue;
                }
		if (gf_asprintf(&key,"trusted.afr.%s",trav->xlator->name) < 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"failed to allocate key");
			goto free_dict;
		}
		value = GF_CALLOC(3,sizeof(*value),gf_by_mt_int32_t);
		if (!value) {
			gf_log (this->name, GF_LOG_WARNING,
				"failed to allocate value");
			goto free_key;
		}
                /* Amazingly, there's no constant for this. */
                value[0] = htons(1);
		if (dict_set_dynptr(dict,key,value,3*sizeof(*value)) < 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"failed to set up dict");
			goto free_value;
		}
	}
        return dict;

free_value:
        GF_FREE(value);
free_key:
        GF_FREE(key);
free_dict:
	dict_unref(dict);
        return NULL;
}

int32_t
bypass_set_pending_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int32_t op_ret, int32_t op_errno, dict_t *dict)
{
	if (op_ret < 0) {
		goto unwind;
	}

	call_resume(cookie);
	return 0;

unwind:
        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, NULL, NULL);
        return 0;
}

int32_t
bypass_writev_resume (call_frame_t *frame, xlator_t *this, fd_t *fd,
                      struct iovec *vector, int32_t count, off_t off,
                      struct iobref *iobref)
{
        bypass_private_t *priv = this->private;

        STACK_WIND (frame, default_writev_cbk, priv->target,
                    priv->target->fops->writev, fd, vector, count, off,
                    iobref);

        return 0;
}

int32_t
bypass_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
               struct iovec *vector, int32_t count, off_t off,
               struct iobref *iobref)
{
	dict_t           *dict = NULL;
	call_stub_t      *stub = NULL;
        bypass_private_t *priv = this->private;

        /*
         * I wish we could just create the stub pointing to the target's
         * writev function, but then we'd get into another translator's code
         * with "this" pointing to us.
         */
	stub = fop_writev_stub(frame, bypass_writev_resume,
			       fd, vector, count, off, iobref);
	if (!stub) {
		gf_log (this->name, GF_LOG_WARNING, "failed to allocate stub");
		goto wind;
	}

        dict = get_pending_dict(this);
        if (!dict) {
		gf_log (this->name, GF_LOG_WARNING, "failed to allocate stub");
                goto free_stub;
        }

	STACK_WIND_COOKIE (frame, bypass_set_pending_cbk, stub,
                           priv->target, priv->target->fops->fxattrop,
                           fd, GF_XATTROP_ADD_ARRAY, dict);
	return 0;

free_stub:
        call_stub_destroy(stub);
wind:
	dict_unref(dict);
        STACK_WIND (frame, default_writev_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->writev, fd, vector, count, off,
                    iobref);
        return 0;
}

/*
 * Even applications that only read seem to call this, and it can force an
 * unwanted self-heal.
 * TBD: there are probably more like this - stat, open(O_RDONLY), etc.
 */
int32_t
bypass_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        bypass_private_t *priv = this->private;

        STACK_WIND (frame, default_fstat_cbk, priv->target,
                    priv->target->fops->fstat, fd);
        return 0;
}

int32_t
init (xlator_t *this)
{
	xlator_t         *tgt_xl = NULL;
	bypass_private_t *priv = NULL;

	if (!this->children || this->children->next) {
		gf_log (this->name, GF_LOG_ERROR, 
			"FATAL: bypass should have exactly one child");
		return -1;
	}

	tgt_xl = this->children->xlator;
	/* TBD: check for cluster/afr as well */
	if (strcmp(tgt_xl->type,"cluster/replicate")) {
		gf_log (this->name, GF_LOG_ERROR,
			"%s must be loaded above cluster/replicate",
                        this->type);
		return -1;
	}
        /* TBD: pass target-translator name as an option (instead of first) */
	tgt_xl = tgt_xl->children->xlator;

	priv = GF_CALLOC (1, sizeof (bypass_private_t), gf_bypass_mt_priv_t);
        if (!priv)
                return -1;

	priv->target = tgt_xl;
	this->private = priv;

	gf_log (this->name, GF_LOG_DEBUG, "bypass xlator loaded");
	return 0;
}

void
fini (xlator_t *this)
{
	bypass_private_t *priv = this->private;

        if (!priv)
                return;
        this->private = NULL;
	GF_FREE (priv);

	return;
}

struct xlator_fops fops = {
        .readv = bypass_readv,
	.writev = bypass_writev,
        .fstat = bypass_fstat
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
	{ .key  = {NULL} },
};
