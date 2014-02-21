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
 * Copyright 2006 Ricardo Correia.
 * Use is subject to license terms.
 */

#ifdef __native_client__
#include <pth/pthread.h>
#else
#include <pthread.h>
#endif

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>

#include "util.h"
#include "fuse_listener.h"

#ifdef NOIOCTL
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <libintl.h>
#include <sys/nvpair.h>
//#include <libuutil.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
//#include <priv.h>
#include <pwd.h>
//#include <zone.h>
#include <sys/fs/zfs.h>
//#include <zfsfuse.h>

#include <sys/stat.h>

//#include <libzfs.h>

#include "new_zpool_util.h"
#include "zfs_comutil.h"

pthread_t storage_create_thread_id;
pthread_t listener_thread_id;

#endif //NOIOCTL

static const char *cf_pidfile = NULL;
static int cf_daemonize = 1;

static void exit_handler(int sig)
{
	exit_fuse_listener = B_TRUE;
}

static int set_signal_handler(int sig, void (*handler)(int))
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(struct sigaction));

	sa.sa_handler = handler;
	sigemptyset(&(sa.sa_mask));
	sa.sa_flags = 0;

	if(sigaction(sig, &sa, NULL) == -1) {
		perror("sigaction");
		return -1;
	}

	return 0;
}

extern char *optarg;
extern int optind, opterr, optopt;

static struct option longopts[] = {
	{ "no-daemon",
	  0, /* has-arg */
	  &cf_daemonize, /* flag */
	  0 /* val */
	},
	{ "pidfile",
	  1,
	  NULL,
	  'p'
	},
	{ "help",
	  0,
	  NULL,
	  'h'
	},
	{ 0, 0, 0, 0 }
};

void print_usage(int argc, char *argv[]) {
	const char *progname = "zfs-fuse";
	if (argc > 0)
		progname = argv[0];
	fprintf(stderr, "Usage: %s [--no-daemon] [-p | --pidfile filename] [-h | --help]\n", progname);
}

static void parse_args(int argc, char *argv[])
{
	int retval;
	while ((retval = getopt_long(argc, argv, "-hp:", longopts, NULL)) != -1) {
		switch (retval) {
			case 1: /* non-option argument passed (due to - in optstring) */
			case 'h':
			case '?':
				print_usage(argc, argv);
				exit(1);
			case 'p':
				if (cf_pidfile != NULL) {
					print_usage(argc, argv);
					exit(1);
				}
				cf_pidfile = optarg;
				break;
			case 0:
				break; /* flag is not NULL */
			default:
				// This should never happen
				fprintf(stderr, "Internal error: Unrecognized getopt_long return 0x%02x\n", retval);
				print_usage(argc, argv);
				exit(1);
				break;
		}
	}
}


static int create_storage(char* storage_path, char* mountdir){
	int ret;
	nvlist_t *nvroot;
	nvlist_t *fsprops = NULL;
	nvlist_t *props = NULL;
	boolean_t force = B_FALSE;
	boolean_t dryrun = B_FALSE;
	char* mountpoint = NULL;

	if ((g_zfs = libzfs_init()) == NULL) {
		(void) fprintf(stderr, gettext("internal error: failed to "
		    "initialize ZFS library\n"));
		return (1);
	}

	/* pass off to get_vdev_spec for bulk processing */
	nvroot = make_root_vdev(NULL, force, !force, B_FALSE, dryrun,
	    1, &storage_path);
	if (nvroot == NULL)
		goto errout;

	/* make_root_vdev() allows 0 toplevel children if there are spares */
	if (!zfs_allocatable_devs(nvroot)) {
		(void) fprintf(stderr, gettext("invalid vdev "
		    "specification: at least one toplevel vdev must be "
		    "specified\n"));
		goto errout;
	}

	/*
	 * Hand off to libzfs.
	 */
	if (zpool_create(g_zfs, mountdir,
			 nvroot, props, fsprops) == 0) {
	    zfs_handle_t *pool = zfs_open(g_zfs, mountdir,
					  ZFS_TYPE_FILESYSTEM);
	    if (pool != NULL) {
		if (mountpoint != NULL)
		    verify(zfs_prop_set(pool,
					zfs_prop_to_name(
							 ZFS_PROP_MOUNTPOINT),
					mountpoint) == 0);
		if (zfs_mount(pool, NULL, 0) == 0)
		    ret = zfs_shareall(pool);
		zfs_close(pool);
	    }
	} else if (libzfs_errno(g_zfs) == EZFS_INVALIDNAME) {
	    (void) fprintf(stderr, gettext("pool name may have "
					   "been omitted\n"));
	}

errout:
	nvlist_free(nvroot);
	nvlist_free(fsprops);
	nvlist_free(props);
	return (ret);
}

#ifdef NOIOCTL
static void* storage_create_thread(void* obj){
	(void)obj;
	create_storage("/home/zvm/zfs.cow", "file");
	return NULL;
}
#endif

int main(int argc, char *argv[])
{
	parse_args(argc, argv);

	if (cf_daemonize) {
		do_daemon(cf_pidfile);
	}

	if(do_init() != 0) {
		do_exit();
		return 1;
	}

	if(set_signal_handler(SIGHUP, exit_handler) != 0 ||
	   set_signal_handler(SIGINT, exit_handler) != 0 ||
	   set_signal_handler(SIGTERM, exit_handler) != 0 ||
	   set_signal_handler(SIGPIPE, SIG_IGN) != 0) {
		do_exit();
		return 2;
	}

#ifdef NOIOCTL
	VERIFY(pthread_create(&storage_create_thread_id, NULL, storage_create_thread, NULL) == 0);
	VERIFY(pthread_create(&listener_thread_id, NULL, zfsfuse_listener_start, NULL) == 0);

	/* wait for all threads to complete */
	int rc = pthread_join(storage_create_thread_id, NULL);
	assert(0 == rc);

	rc = pthread_join(listener_thread_id, NULL);
	assert(0 == rc);
#else
	int ret = zfsfuse_listener_start();
#endif

	do_exit();

	return 0;
}
