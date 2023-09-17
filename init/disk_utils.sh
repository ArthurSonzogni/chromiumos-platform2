#!/bin/sh
# Copyright 2015 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

STATEFUL="/mnt/stateful_partition"

get_stateful_df_data() {
  local bs="${1:-1K}"
  df --block-size "${bs}" -P "${STATEFUL}" | grep -m 1 "${STATEFUL}"
}

# Get the lifetime writes from the stateful partition.
get_stateful_lifetime_writes() {
  local stateful_dev
  stateful_dev="$(rootdev '/mnt/stateful_partition' | sed -e 's#^/dev/##')"
  local lifetime_writes
  lifetime_writes="$(cat "/sys/fs/ext4/${stateful_dev}/lifetime_write_kbytes")"
  : "${lifetime_writes:=0}"
  echo "${lifetime_writes}"
}

# Get the percentage of space used on the stateful partition.
get_stateful_usage_percent() {
  local total free used
  total=$(get_stateful_total_space_blocks)
  free=$(get_stateful_free_space_blocks "${bs}")
  used=$(( total - free ))
  echo $(( used * 100 / total ))
}

# Get the free space on the stateful partition.
#
# inputs:
#   bs        -- size of block as understood by strosize (suffixes allowed)
get_stateful_free_space_blocks() {
  local bs bs_bytes size
  bs="${1:-1K}"
  bs_bytes=$(numfmt --from=iec "${bs}")
  size=$(spaced_cli --get_free_disk_space="${STATEFUL}")
  echo $(( size / bs_bytes ))
}

# Get the total space on the stateful partition.
#
# inputs:
#   bs        -- size of block as understood by strosize (suffixes allowed)
get_stateful_total_space_blocks() {
  local bs bs_bytes size
  bs="${1:-1K}"
  bs_bytes=$(numfmt --from=iec "${bs}")
  size=$(spaced_cli --get_total_disk_space="${STATEFUL}")
  echo $(( size / bs_bytes ))
}

# Get the used space on the stateful partition.
#
# inputs:
#   bs        -- size of block as understood by strosize (suffixes allowed)
get_stateful_used_space_blocks() {
  local bs total free
  bs="${1:-1K}"
  total=$(get_stateful_total_space_blocks "${bs}")
  free=$(get_stateful_free_space_blocks "${bs}")
  echo $(( total - free ))
}

# Gets enum for stateful partition's format.
#
# Output denotes the following formats:
#   0 - Raw partition
#   1 - Logical volume (LVM)
get_stateful_format_enum() {
  local stateful_dev
  stateful_dev="$(rootdev '/mnt/stateful_partition')"

  case "${stateful_dev}" in
    /dev/dm*) printf 1 ;;
    *)        printf 0 ;;
  esac
}
