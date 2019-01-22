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

#define DEBUG 1

#ifdef DEBUG
#define pr_debug(fmt, ...) printf(fmt, __VA_ARGS__)
static void hexdump(void *data, size_t size)
{
	char ascii[17];
	size_t i, j;
	ascii[16] = '\0';

	for (i = 0; i < size; ++i) {
		if ((i % 16) == 0)
			printf("%04lx: ", i);
		printf("%02x ", ((unsigned char*)data)[i]);
		if (((unsigned char*)data)[i] >= ' '
		    && ((unsigned char*)data)[i] <= '~') {
			ascii[i % 16] = ((unsigned char*)data)[i];
		} else {
			ascii[i % 16] = '.';
		}

		if ((i+1) % 8 == 0 || i+1 == size) {
			printf(" ");
			if ((i+1) % 16 == 0) {
				printf("|  %s \n", ascii);
			} else if (i+1 == size) {
				ascii[(i+1) % 16] = '\0';
				if ((i+1) % 16 <= 8) {
					printf(" ");
				}
				for (j = (i+1) % 16; j < 16; ++j) {
					printf("   ");
				}
				printf("|  %s \n", ascii);
			}
		}
	}
}
#else
#define pr_debug(fmt, ...) do {} while (0)
static inline void hexdump(void *data, size_t size) { }
#endif

static int btrfs_lookup_csum(struct btrfs_root *root, struct btrfs_path *path,
			     u64 bytenr, u64 extent_len)
{
	struct btrfs_key key;
	int pending_csums;
	int total_csums;
	u16 csum_size;
	int ret;

	key.objectid = BTRFS_EXTENT_CSUM_OBJECTID;
	key.offset = bytenr;
	key.type = BTRFS_EXTENT_CSUM_KEY;

	csum_size = btrfs_super_csum_size(root->fs_info->super_copy);

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

	pending_csums = total_csums = extent_len / 4096;

	ret = -EINVAL;
	while (pending_csums) {
		struct extent_buffer *leaf;
		struct btrfs_key found_key;
		struct btrfs_csum_item *ci;
		u32 item_size;
		int nr_csums;
		u32 offset;
		int slot;
		u8 *buf;
		int i;

		leaf = path->nodes[0];
		slot = path->slots[0];

		btrfs_item_key_to_cpu(leaf, &found_key, slot);
		if (found_key.type != BTRFS_EXTENT_CSUM_KEY)
			goto out;

		item_size = btrfs_item_size_nr(leaf, slot);
		offset = btrfs_item_offset_nr(leaf, slot);
		nr_csums = item_size / 4;

		buf = calloc(sizeof(u8), nr_csums);
		if (!buf) {
			ret = -ENOMEM;
			break;
		}

		pr_debug("%s: item_size: %d, offset: %d\n",
		       __func__, item_size, offset);
		read_extent_buffer(leaf, buf, offset, item_size);
		hexdump(buf, item_size);

		if (pending_csums < nr_csums) {
			pr_debug("btrfs_csum_item contains csums for more than one extent, %d - %d\n",
			       item_size / csum_size, pending_csums);
			for (i = 0; i < pending_csums; i++) {
				u32 pos = i * sizeof(struct btrfs_csum_item);

				ci = (struct btrfs_csum_item *) buf + pos;
				pr_debug("CSUM 0x%08x\n", (u32)ci->csum);
			}
			pending_csums = 0;
		} else {
			for (i = 0; i < nr_csums; i++) {
				u32 pos = i * sizeof(struct btrfs_csum_item);

				ci = (struct btrfs_csum_item *) buf + pos;
				pr_debug("CSUM 0x%08x\n", (u32)ci->csum);
			}
			pending_csums -= nr_csums;
		}

		ret = btrfs_next_item(root, path);
		if (ret > 0) {
			ret = 0;
			break;
		}
	}

out:
	if (ret)
		error("failed to lookup checksums for extent at %llu", bytenr);

	return ret;
}

static int btrfs_get_extent_csum(struct btrfs_root *root,
				 struct btrfs_path *path, unsigned long ino)
{
	struct btrfs_fs_info *info = root->fs_info;
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
		bytenr = btrfs_file_extent_disk_bytenr(leaf, fi);

		pr_debug("%s: extent_len: %llu\n", __func__, extent_len);
		pr_debug("%s: bytenr: %llu\n", __func__, bytenr);

		csum_path = btrfs_alloc_path();
		ret = btrfs_lookup_csum(info->csum_root, csum_path, bytenr,
					extent_len);
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

	pr_debug("%s: '%s' is on subvolume %llu\n", __func__, filename, rootid);

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
	ret = btrfs_get_extent_csum(root, &path, sb.st_ino);
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
