# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

STATEFUL="/mnt/stateful_partition"

get_stateful_df_data() {
  local bs="${1:-1K}"
  df --block-size "${bs}" -P "${STATEFUL}" | grep -m 1 "${STATEFUL}"
}

# Get the percentage of space used on the stateful partition.
get_stateful_usage_percent() {
  local stateful_space="$(get_stateful_df_data)"
  # Remove everything after and including the "%"
  stateful_space="${stateful_space%%%*}"
  # Remove all fields except the last one.
  stateful_space="${stateful_space##* }"
  echo "${stateful_space}"
}

# Get the free space on the stateful partition.
#
# inputs:
#   bs        -- size of block as understood by strosize (suffixes allowed)
get_stateful_free_space_blocks() {
  local bs="${1:-1K}"
  get_stateful_df_data "${bs}" | awk '{print $4}'
}

# Get the total space on the stateful partition.
#
# inputs:
#   bs        -- size of block as understood by strosize (suffixes allowed)
get_stateful_total_space_blocks() {
  local bs="${1:-1K}"
  get_stateful_df_data "${bs}" | awk '{print $2}'
}
