/*
 * Copyright (C) 2018 SUSE. All rights reserved.
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

#include "ctree.h"
#include "disk-io.h"
#include "volumes.h"
#include "help.h"

static int btrfs_get_extent_csum(struct btrfs_fs_info *info,
				 struct btrfs_path *path, unsigned long ino)
{
	struct btrfs_root *fs_root = info->fs_root;
	struct btrfs_file_extent_item *fei;
	struct extent_buffer *leaf;
	struct btrfs_key found_key;
	struct btrfs_key key;
	int slot;
	int ret;

	key.objectid = ino;
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = 0;

	ret = btrfs_search_slot(NULL, fs_root, &key, path, 0, 0);
	if (ret) {
		fprintf(stderr, "No extents found for inode: %lu\n", ino);
		goto out;
	}

	while (1) {
		u64 nr_bytes;
		u64 nr_csums;
		u64 bytenr;

		leaf = path->nodes[0];
		slot = path->slots[0];

		if (slot >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(fs_root, path);
			if (ret == 0)
				continue;
			if (ret < 0)
				goto out;
		}

		btrfs_item_key_to_cpu(leaf, &found_key, slot);
		if (found_key.type != BTRFS_EXTENT_DATA_KEY) {
			ret = -EINVAL;
			goto out;
		}

		path->slots[0]++;

		fei = btrfs_item_ptr(leaf, slot,
				struct btrfs_file_extent_item);
		bytenr = btrfs_file_extent_disk_bytenr(leaf, fei);
		nr_bytes = btrfs_file_extent_num_bytes(leaf, fei);

		nr_csums = nr_bytes / 1024 / 4;

		printf("ino: %lu, nr_bytes: %llu, nr_csums: %llu, bytenr: %lld\n",
		       ino, nr_bytes, nr_csums, bytenr);
	}

out:
	btrfs_release_path(path);
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
	struct btrfs_path path;
	struct stat sb;
	char *filename;
	int ret;

	ret = check_argc_exact(argc, 3);
	if (ret)
		usage(cmd_inspect_dump_csum_usage);

	filename = argv[1];

	info = open_ctree_fs_info(argv[2], 0, 0, 0, OPEN_CTREE_PARTIAL);
	if (!info) {
		fprintf(stderr, "unable to open device %s\n", argv[2]);
		exit(1);
	}

	ret = stat(filename, &sb);
	if (ret) {
		fprintf(stderr, "Cannot stat %s: %s\n",
			filename, strerror(errno));
		exit(1);
	}

	if (sb.st_size < 1024) {
		fprintf(stderr, "File smaller than 1KB, aborting\n");
		exit(1);
	}

	btrfs_init_path(&path);
	ret = btrfs_get_extent_csum(info, &path, sb.st_ino);
	close_ctree(info->fs_root);
	btrfs_close_all_devices();

	return ret;
}
