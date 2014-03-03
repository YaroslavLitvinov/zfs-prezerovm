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
//#include "fuse_common.h"
//#include "fuse_listener.h"

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
#include <sys/mount.h>
#include <sys/systm.h>
#include <sys/stat.h>

#include <libzfs.h>

#include "zfs_comutil.h"
#include "zfs_filesystem.h"
#include "toplevel_filesystem.h"
#include "open_file_description.h"
#include "new_zpool_util.h"
#include "handle_allocator.h"
#include "dirent_engine.h"

pthread_t storage_create_thread_id;
//pthread_t listener_thread_id;

#endif //NOIOCTL

static const char *cf_pidfile = NULL;
static int cf_daemonize = 1;

extern char *optarg;
extern int optind, opterr, optopt;

extern vfsops_t *zfs_vfsops;
extern vfs_t*  s_vfs;
vfs_t*  s_vfs;

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


static vfs_t* prepare_storage(zfs_handle_t *zfs_handle, char* name, char* mountdir){
	char mountpoint[ZFS_MAXPROPLEN];

	if (!zfs_is_mountable(zfs_handle, mountpoint, sizeof (mountpoint), NULL))
	    return (0);

	vfs_t *vfs = kmem_zalloc(sizeof(vfs_t), KM_SLEEP);
	if(vfs == NULL){
	    errno = ENOMEM;
	    return NULL;
	}

	VFS_INIT(vfs, zfs_vfsops, 0);
	VFS_HOLD(vfs);

	struct mounta uap = {name, mountdir, MS_SYSSPACE, NULL, "", 0};
	int ret;
	if ((ret = VFS_MOUNT(vfs, rootdir, &uap, kcred)) != 0) {
	    kmem_free(vfs, sizeof(vfs_t));
	    return NULL;
	}
	return vfs;
}

static int create_storage(char* storage_path, char* name, char* mountdir){
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
	if (zpool_create(g_zfs, name,
			 nvroot, props, fsprops) == 0) {
	    zfs_handle_t *pool = zfs_open(g_zfs, name,
					  ZFS_TYPE_FILESYSTEM);
	    if (pool != NULL) {
		if (mountpoint != NULL)
		    verify(zfs_prop_set(pool,
					zfs_prop_to_name(
							 ZFS_PROP_MOUNTPOINT),
					mountpoint) == 0);
		s_vfs = prepare_storage(pool, name, mountdir);
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

static void* storage_create_thread(void* obj){
	(void)obj;
	create_storage("/home/zvm/zfs.cow", "file", "/file");
	return NULL;
}

static struct MountsPublicInterface* create_filesystem_interface(){
    struct DirentEnginePublicInterface* dirent_engine = 
	INSTANCE_L(DIRENT_ENGINE)();

    assert(s_vfs);

    struct LowLevelFilesystemPublicInterface* zfs_lowlevel_fs = 
	CONSTRUCT_L(ZFS_FILESYSTEM)( s_vfs, dirent_engine );

    struct MountsPublicInterface* toplevel_fs = 
	CONSTRUCT_L(TOPLEVEL_FILESYSTEM)( get_handle_allocator(),
					  get_open_files_pool(),
					  zfs_lowlevel_fs);
    return toplevel_fs;
}


/* static struct fuse_operations zfs_oper = { */
/* 	.getattr	= zfs_getattr, */
/* 	.access		= zfs_access, */
/* 	.readlink	= zfs_readlink, */
/* 	.readdir	= zfs_readdir, */
/* 	.mknod		= zfs_mknod, */
/* 	.mkdir		= zfs_mkdir, */
/* 	.symlink	= zfs_symlink, */
/* 	.unlink		= zfs_unlink, */
/* 	.rmdir		= zfs_rmdir, */
/* 	.rename		= zfs_rename, */
/* 	.link		= zfs_link, */
/* 	.chmod		= zfs_chmod, */
/* 	.chown		= zfs_chown, */
/* 	.truncate	= zfs_truncate, */
/* 	.utimens	= zfs_utimens, */
/* 	.open		= zfs_open, */
/* 	.read		= zfs_read, */
/* 	.write		= zfs_write, */
/* 	.statfs		= zfs_statfs, */
/* 	.release	= zfs_release */
/* }; */


int main(int argc, char *argv[])
{
	int ret;
	parse_args(argc, argv);

	if (cf_daemonize) {
		do_daemon(cf_pidfile);
	}

	if(do_init() != 0) {
		do_exit();
		return 1;
	}

	VERIFY(pthread_create(&storage_create_thread_id, NULL, storage_create_thread, NULL) == 0);

	/* wait for all threads to complete */
	ret = pthread_join(storage_create_thread_id, NULL);
	assert(0 == ret);

	struct MountsPublicInterface* fs = create_filesystem_interface();
	assert(fs);
	


	//ret = fuse_main(argc, argv, &zfs_oper, NULL);

	do_exit();

	return ret;
}
