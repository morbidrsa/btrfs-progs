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
#include "help.h"

const char * const cmd_inspect_dump_csum_usage[] = {
	"btrfs inspect-internal dump-csum <path> <device>",
	"Get Checksums for a given file",
	NULL
};

int cmd_inspect_dump_csum(int argc, char **argv)
{
	struct btrfs_fs_info *info;
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

	return 0;
}
