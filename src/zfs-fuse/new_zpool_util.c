/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */



#include <errno.h>
#include <libgen.h>
#include <libintl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include <sys/types.h>
#include <sys/mnttab.h>
#include "new_zpool_util.h"

#ifdef NOIOCTL
#undef getmntent

int
getmntany(FILE *fp, struct mnttab *mgetp, struct mnttab *mrefp)
{
    abort();
    return 0;
}

int
_sol_getmntent(FILE *fp, struct mnttab *mgetp)
{
    abort();
    return 0;
}

int getextmntent(FILE *fp, struct extmnttab *mp, int len)
{
    abort();
    return 0;
}


int
mkdirp(const char *d, mode_t mode)
{
    return mkdir(d, mode);
}

size_t zprop_width(int a, boolean_t *b, zfs_type_t c){
    abort();
    return 0;
}
#endif

/*
 * Utility function to guarantee malloc() success.
 */
void *
safe_malloc(size_t size)
{
	void *data;

	if ((data = calloc(1, size)) == NULL) {
		(void) fprintf(stderr, "internal error: out of memory\n");
		exit(1);
	}

	return (data);
}

/*
 * Same as above, but for strdup()
 */
char *
safe_strdup(const char *str)
{
	char *ret;

	if ((ret = strdup(str)) == NULL) {
		(void) fprintf(stderr, "internal error: out of memory\n");
		exit(1);
	}

	return (ret);
}

/*
 * Display an out of memory error message and abort the current program.
 */
void
zpool_no_memory(void)
{
	assert(errno == ENOMEM);
	(void) fprintf(stderr,
	    gettext("internal error: out of memory\n"));
	exit(1);
}

/*
 * Return the number of logs in supplied nvlist
 */
uint_t
num_logs(nvlist_t *nv)
{
	uint_t nlogs = 0;
	uint_t c, children;
	nvlist_t **child;

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		return (0);

	for (c = 0; c < children; c++) {
		uint64_t is_log = B_FALSE;

		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
		    &is_log);
		if (is_log)
			nlogs++;
	}
	return (nlogs);
}
