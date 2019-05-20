#!/bin/bash
# Check various combinations of file layouts and dump it's checksums, fail if
# the checksum does not match the known good value for the pattern.

source "$TEST_TOP/common"

check_prereq mkfs.btrfs
check_prereq btrfs

setup_root_helper
prepare_test_dev

printf "%0.s\xcd" {1..12288} > input

# Create a pristine file-system to use
run_check $SUDO_HELPER "$TOP/mkfs.btrfs" -f "$TEST_DEV"
run_check_mount_test_dev

# 12k extent alone - 3x 0x25767598 must be written as 4k blocks
run_check $SUDO_HELPER dd if=input of=$TEST_MNT/12k oflag=direct bs=4k \
	> /dev/null 2>&1
run_check_stdout $SUDO_HELPER "$TOP/btrfs" inspect-internal dump-csum \
	$TEST_MNT/12k $TEST_DEV | grep -oE "0x[0-9a-z]{8}$" | while read CSUM
	do
		if [ "$CSUM" != "0x25767598" ]; then
			_fail "invalid checksum $CSUM expected 0x25767598"
		fi
done

rm -f input

run_check_umount_test_dev

