// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "update_engine/payload_consumer/install_plan.h"

namespace chromeos_update_engine {

TEST(InstallPlanTest, Dump) {
  InstallPlan install_plan{
      .download_url = "foo-download-url",
      .version = "foo-version",
      .payloads = {{
          .payload_urls = {"url1", "url2"},
          .metadata_signature = "foo-signature",
          .hash = {0xb2, 0xb3},
          .fp = "foo-fp",
          .app_id = "foo-app-id",
      }},
      .source_slot = BootControlInterface::kInvalidSlot,
      .target_slot = BootControlInterface::kInvalidSlot,
      .partitions = {{
          .name = "foo-partition_name",
          .source_path = "foo-source-path",
          .source_hash = {0xb1, 0xb2},
          .target_path = "foo-target-path",
          .target_hash = {0xb3, 0xb4},
          .postinstall_path = "foo-path",
          .filesystem_type = "foo-type",
      }},
  };

  EXPECT_EQ(install_plan.ToString(),
            R"(type: new_update
version: foo-version
source_slot: INVALID
target_slot: INVALID
minios_target_slot: INVALID
minios_source_slot: INVALID
initial url: foo-download-url
hash_checks_mandatory: true
signature_checks_mandatory: true
powerwash_required: false
switch_slot_on_reboot: true
switch_minios_slot: false
run_post_install: true
is_rollback: false
rollback_data_save_requested: false
write_verity: true
can_download_be_canceled: false
Partition: foo-partition_name
  source_size: 0
  source_path: foo-source-path
  source_hash: B1B2
  target_size: 0
  target_path: foo-target-path
  target_hash: B3B4
  run_postinstall: false
  postinstall_path: foo-path
  filesystem_type: foo-type
Payload: 0
  urls: (url1,url2)
  size: 0
  metadata_size: 0
  metadata_signature: foo-signature
  hash: B2B3
  type: unknown
  fingerprint: foo-fp
  app_id: foo-app-id
  already_applied: false)");
}

}  // namespace chromeos_update_engine
