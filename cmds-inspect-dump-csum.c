/*
 * Copyright (C) 2019 SUSE. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include "ctree.h"
#include "disk-io.h"
#include "volumes.h"
#include "help.h"
#include "utils.h"

static int btrfs_lookup_csum(struct btrfs_root *root, struct btrfs_path *path,
			     u64 bytenr, int total_csums)
{
	struct btrfs_key key;
	int ret;

	key.objectid = BTRFS_EXTENT_CSUM_OBJECTID;
	key.offset = bytenr;
	key.type = BTRFS_EXTENT_CSUM_KEY;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto fail;

fail:
	error("failed to lookup checksums for extent at %llu", bytenr);

	return 0;
}

static int btrfs_get_extent_csum(struct btrfs_fs_info *info,
				 struct btrfs_root *root,
				 struct btrfs_path *path, unsigned long ino)
{
	u16 csum_size = btrfs_super_csum_size(info->super_copy);
	struct btrfs_key key;
	int ret;

	key.objectid = ino;
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = 0;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0) {
		goto out;
	} else if (ret > 0) {
		if (path->slots[0] == 0) {
			ret = -ENOENT;
			goto out;
		} else {
			path->slots[0]--;
		}
	}

	ret = -EINVAL;
	while (1) {
		struct btrfs_file_extent_item *fi;
		struct btrfs_path *csum_path;
		struct extent_buffer *leaf;
		struct btrfs_key found_key;
		int total_csums;
		u64 extent_len;
		u64 bytenr;
		int slot;

		leaf = path->nodes[0];
		slot = path->slots[0];

		btrfs_item_key_to_cpu(leaf, &found_key, slot);
		if (found_key.type != BTRFS_EXTENT_DATA_KEY)
			goto out;

		fi = btrfs_item_ptr(leaf, slot, struct btrfs_file_extent_item);
		extent_len = btrfs_file_extent_num_bytes(leaf, fi);
		bytenr = btrfs_file_extent_num_bytes(leaf, fi);
		total_csums = extent_len / 1024 / csum_size;

		printf("%s: extent_len: %llu\n", __func__, extent_len);
		printf("%s: bytenr: %llu\n", __func__, bytenr);

		csum_path = btrfs_alloc_path();
		ret = btrfs_lookup_csum(info->csum_root, csum_path, bytenr,
					total_csums);
		btrfs_release_path(csum_path);
		if (ret) {
			error("Error looking up checsum\n");
			break;
		}

		ret = btrfs_next_item(root, path);
		if (ret > 0) {
			ret = 0;
			break;
		}
	}

out:
	return ret;
}

const char * const cmd_inspect_dump_csum_usage[] = {
	"btrfs inspect-internal dump-csum <path> <device>",
	"Get Checksums for a given file",
	NULL
};

int cmd_inspect_dump_csum(int argc, char **argv)
{
	struct btrfs_fs_info *info;
	struct btrfs_root *root;
	struct btrfs_path path;
	struct stat sb;
	char *filename;
	u64 rootid;
	int fd;
	int ret;

	ret = check_argc_exact(argc, 3);
	if (ret)
		usage(cmd_inspect_dump_csum_usage);

	filename = argv[1];

	info = open_ctree_fs_info(argv[2], 0, 0, 0, OPEN_CTREE_PARTIAL);
	if (!info) {
		error("unable to open device %s\n", argv[2]);
		exit(1);
	}

	ret = stat(filename, &sb);
	if (ret) {
		error("cannot stat %s: %s\n", filename, strerror(errno));
		exit(1);
	}

	if (sb.st_size < 1024) {
		error("file smaller than 1KB, aborting\n");
		exit(1);
	}

	fd = open(filename, O_RDONLY);
	ret = lookup_path_rootid(fd, &rootid);
	if (ret) {
		error("error looking up subvolume for '%s'\n",
				filename);
		goto out_close;
	}

	printf("%s: '%s' is on subvolume %llu\n", __func__, filename, rootid);

	root = info->fs_root;

	if (rootid != BTRFS_FS_TREE_OBJECTID) {
		struct btrfs_key key;

		key.objectid = rootid;
		key.type = BTRFS_ROOT_ITEM_KEY;
		key.offset = (u64)-1;

		root = btrfs_read_fs_root(info, &key);
		if (IS_ERR(root)) {
			error("unable to read root of subvolume '%llu'\n",
			      rootid);
			goto out_close;
		}
	}

	btrfs_init_path(&path);
	ret = btrfs_get_extent_csum(info, root, &path, sb.st_ino);
	btrfs_release_path(&path);
	close_ctree(info->fs_root);
	btrfs_close_all_devices();

	if (ret)
		error("checsum lookup for file %s (%lu) failed\n",
			filename, sb.st_ino);
out_close:
	close(fd);
	return ret;
}
