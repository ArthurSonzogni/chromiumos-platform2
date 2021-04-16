#!/bin/sh
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Convenience script to regenerate all auto-generated files (test
# data, README.md, power manager schema).

set -e

# Change to the directory of this script.
cd "$(dirname "$0")"

# Regen power manager prefs schema
python3 -m cros_config_host.power_manager_prefs_gen_schema \
        -o cros_config_host/power_manager_prefs_schema.yaml

# Regen README (must come after power manager prefs, as this can
# affect the schema content)
python3 -m cros_config_host.generate_schema_doc -o README.md

python3 -m cros_config_host.cros_config_schema -c test_data/test_import.yaml \
        -o test_data/test_import.json
python3 -m cros_config_host.cros_config_schema -o test_data/test_merge.json \
        -m test_data/test_merge_base.yaml test_data/test_merge_overlay.yaml

regen_test_data_with_c_bindings() {
    python3 -m cros_config_host.cros_config_schema -f True \
            -c "test_data/${1}.yaml" -o "test_data/${1}.json" \
            -g test_data
    # TODO(jrosenth): cros_config_schema doesn't let us specify where to
    # put the C file directly?  Refactor later.
    mv test_data/config.c "test_data/${1}.c"
}

# ARM test data
regen_test_data_with_c_bindings test_arm

# x86 test data
regen_test_data_with_c_bindings test
