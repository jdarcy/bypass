/*
 * Copyright (c) 2011 Red Hat <http://www.redhat.com>
 */

#ifndef __bypass_H__
#define __bypass_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif
#include "mem-types.h"

/* Deal with casts for 32-bit architectures. */
#define CAST2INT(x) ((uint64_t)(long)(x))
#define CAST2PTR(x) ((void *)(long)(x))

typedef struct {
        xlator_t *target;
} bypass_private_t;

enum gf_bypass_mem_types_ {
        gf_bypass_mt_priv_t = gf_common_mt_end + 1,
        gf_by_mt_int32_t,
        gf_bypass_mt_end
};

#endif /* __bypass_H__ */
