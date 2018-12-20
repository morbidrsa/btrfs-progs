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

#include <stdlib.h>

const char * const cmd_inspect_csum_dump_usage[] = {
	"btrfs inspect-internal csum-dump <path>",
	"Get Checksums for a given file",
	NULL
};

int cmd_inspect_csum_dump(int argc, char **argv)
{
	return 0;
}
