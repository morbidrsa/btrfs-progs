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

#include <linux/fiemap.h>
#include <linux/fs.h>

#include <sys/types.h>
#include <sys/ioctl.h>

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <getopt.h>

#include "kerncompat.h"
#include "ctree.h"
#include "messages.h"
#include "help.h"
#include "ioctl.h"
#include "utils.h"
#include "disk-io.h"

static bool debug = false;

static int btrfs_lookup_csum_for_extent(int fd, struct btrfs_super_block *sb,
					struct fiemap_extent *fe)
{
	struct btrfs_ioctl_search_args_v2 *search;
	struct btrfs_ioctl_search_key *sk;
	const int bufsz = 1024;
	char buf[bufsz], *bp;
	unsigned int off = 0;
	const int csum_size = btrfs_super_csum_size(sb);
	const int sector_size = btrfs_super_sectorsize(sb);
	int ret, i, j;
	u64 phys = fe->fe_physical;
	u64 needle = phys;
	u64 pending_csum_count = fe->fe_length / sector_size;

	memset(buf, 0, sizeof(buf));
	search = (struct btrfs_ioctl_search_args_v2 *)buf;
	sk = &search->key;

again:
	if (debug)
		printf(
"Looking up checksums for extent at physial offset: %llu (searching at %llu), looking for %llu csums\n",
		       phys, needle, pending_csum_count);

	sk->tree_id = BTRFS_CSUM_TREE_OBJECTID;
	sk->min_objectid = BTRFS_EXTENT_CSUM_OBJECTID;
	sk->max_objectid = BTRFS_EXTENT_CSUM_OBJECTID;
	sk->max_type = BTRFS_EXTENT_CSUM_KEY;
	sk->min_type = BTRFS_EXTENT_CSUM_KEY;
	sk->min_offset = needle;
	sk->max_offset = (u64)-1;
	sk->max_transid = (u64)-1;
	sk->nr_items = 1;
	search->buf_size = bufsz - sizeof(*search);

	ret = ioctl(fd, BTRFS_IOC_TREE_SEARCH_V2, search);
	if (ret < 0)
		return ret;

	/*
	 * If we don't find the csum item at @needle go back by @sector_size and
	 * retry until we've found it.
	 */
	if (sk->nr_items == 0) {
		needle -= sector_size;
		goto again;
	}


	bp = (char *) search->buf;

	for (i = 0; i < sk->nr_items; i++) {
		struct btrfs_ioctl_search_header *sh;
		u64 csums_in_item;

		sh = (struct btrfs_ioctl_search_header *) (bp + off);
		off += sizeof(*sh);

		csums_in_item = btrfs_search_header_len(sh) / csum_size;
		csums_in_item = min(csums_in_item, pending_csum_count);

		for (j = 0; j < csums_in_item; j++) {
			struct btrfs_csum_item *csum_item;

			csum_item = (struct btrfs_csum_item *)
						(bp + off + j * csum_size);

			printf("Offset: %llu, checksum: 0x%08x\n",
			       phys + j * sector_size, *(u32 *)csum_item);
		}

		off += btrfs_search_header_len(sh);
		pending_csum_count -= csums_in_item;

	}

	return ret;
}

static int btrfs_get_extent_csum(int fd, struct btrfs_super_block *sb)
{
	struct fiemap *fiemap, *tmp;
	size_t ext_size;
	int ret, i;

	fiemap = calloc(1, sizeof(*fiemap));
	if (!fiemap)
		return -ENOMEM;

	fiemap->fm_length = ~0;

	ret = ioctl(fd, FS_IOC_FIEMAP, fiemap);
	if (ret)
		goto free_fiemap;

	ext_size = fiemap->fm_mapped_extents * sizeof(struct fiemap_extent);

	tmp = realloc(fiemap, sizeof(*fiemap) + ext_size);
	if (!tmp) {
		ret = -ENOMEM;
		goto free_fiemap;
	}

	fiemap = tmp;
	fiemap->fm_extent_count = fiemap->fm_mapped_extents;
	fiemap->fm_mapped_extents = 0;

	ret = ioctl(fd, FS_IOC_FIEMAP, fiemap);
	if (ret)
		goto free_fiemap;

	for (i = 0; i < fiemap->fm_mapped_extents; i++) {

		ret = btrfs_lookup_csum_for_extent(fd, sb,
						   &fiemap->fm_extents[i]);
		if (ret)
			break;
	}


free_fiemap:
	free(fiemap);
	return ret;
}

const char * const cmd_inspect_dump_csum_usage[] = {
	"btrfs inspect-internal dump-csum <path> <device>",
	"Get Checksums for a given file",
	"-d|--debug    Be more verbose",
	NULL
};

int cmd_inspect_dump_csum(int argc, char **argv)
{
	struct btrfs_super_block sb;
	char *filename;
	char *device;
	int fd;
	int devfd;
	int ret;

	optind = 0;

	while (1) {
		static const struct option longopts[] = {
			{ "debug", no_argument, NULL, 'd' },
			{ NULL, 0, NULL, 0 }
		};

		int opt = getopt_long(argc, argv, "d", longopts, NULL);
		if (opt < 0)
			break;

		switch (opt) {
		case 'd':
			debug = true;
			break;
		default:
			usage(cmd_inspect_dump_csum_usage);
		}
	}

	if (check_argc_exact(argc - optind, 2))
		usage(cmd_inspect_dump_csum_usage);

	filename = argv[optind];
	device = argv[optind + 1];

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		error("cannot open file %s:%m", filename);
		return -errno;
	}

	devfd = open(device, O_RDONLY);
	if (devfd < 0) {
		ret = -errno;
		goto out_close;
	}
	load_sb(devfd, btrfs_sb_offset(0), &sb, sizeof(sb));
	close(devfd);

	if (btrfs_super_magic(&sb) != BTRFS_MAGIC) {
		ret = -EINVAL;
		error("bad magic on superblock on %s", device);
		goto out_close;
	}

	ret = btrfs_get_extent_csum(fd, &sb);
	if (ret)
		error("checsum lookup for file %s failed", filename);
out_close:
	close(fd);
	return ret;
}
