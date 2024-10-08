// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CROS_OMAHA_RESPONSE_H_
#define UPDATE_ENGINE_CROS_OMAHA_RESPONSE_H_

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <limits>
#include <string>
#include <vector>

namespace chromeos_update_engine {

// This struct encapsulates the data Omaha's response for the request.
// The strings in this struct are not XML escaped.
struct OmahaResponse {
  // True iff there is an update to be downloaded.
  bool update_exists = false;

  // If non-zero, server-dictated poll interval in seconds.
  int poll_interval = 0;

  // These are only valid if update_exists is true:
  std::string version;

  struct Package {
    // The ordered list of URLs in the Omaha response. Each item is a complete
    // URL (i.e. in terms of Omaha XML, each value is a urlBase + packageName)
    std::vector<std::string> payload_urls;
    uint64_t size = 0;
    uint64_t metadata_size = 0;
    std::string metadata_signature;
    std::string hash;
    // True if the payload described in this response is a delta payload.
    // False if it's a full payload.
    bool is_delta = false;
    // True if the payload can be excluded from updating if consistently faulty.
    // False if the payload is critical to update.
    bool can_exclude = false;
    // The App ID associated with the package.
    std::string app_id;
    // The unique fingerprint value associated with the package.
    std::string fp;
  };
  std::vector<Package> packages;

  std::string more_info_url;
  std::string deadline;
  int max_days_to_scatter = 0;
  // The number of URL-related failures to tolerate before moving on to the
  // next URL in the current pass. This is a configurable value from the
  // Omaha Response attribute, if ever we need to fine tune the behavior.
  uint32_t max_failure_count_per_url = 0;
  bool prompt = false;

  // True if the Omaha rule instructs us to disable the back-off logic
  // on the client altogether. False otherwise.
  bool disable_payload_backoff = false;

  // True if the Omaha rule instructs us to disable p2p for downloading.
  bool disable_p2p_for_downloading = false;

  // True if the Omaha rule instructs us to disable p2p for sharing.
  bool disable_p2p_for_sharing = false;

  // We sometimes need to waive the hash checks in order to download from
  // sources that don't provide hashes or when we want to explicitly waive hash
  // checking because of an internal algorithm error.
  bool disable_hash_checks = false;

  // True if the Omaha rule instructs us to powerwash.
  bool powerwash_required = false;

  // Whether we continue checking for updates.
  bool disable_repeated_updates = false;

  // Whether we need to invalidate the previous update. This only applies to
  // OS updates.
  bool invalidate_last_update = false;

  // If not blank, a base-64 encoded representation of the PEM-encoded
  // public key in the response.
  std::string public_key_rsa;

  // If not -1, the number of days since the epoch Jan 1, 2007 0:00
  // PST, according to the Omaha Server's clock and timezone (PST8PDT,
  // aka "Pacific Time".)
  int install_date_days = -1;

  // True if the returned image is a rollback for the device.
  bool is_rollback = false;

  // If not empty, contains the reason why Omaha did not send an update.
  std::string no_update_reason;

  struct RollbackKeyVersion {
    // Kernel key version. 0xffff if the value is unknown.
    uint16_t kernel_key = std::numeric_limits<uint16_t>::max();
    // Kernel version. 0xffff if the value is unknown.
    uint16_t kernel = std::numeric_limits<uint16_t>::max();
    // Firmware key verison. 0xffff if the value is unknown.
    uint16_t firmware_key = std::numeric_limits<uint16_t>::max();
    // Firmware version. 0xffff if the value is unknown.
    uint16_t firmware = std::numeric_limits<uint16_t>::max();
  };

  // True if the update is a migration.
  bool migration = false;

  // Key versions of the returned rollback image. Values are 0xffff if the
  // image not a rollback, or the fields were not present.
  RollbackKeyVersion rollback_key_version;

  // Key versions of the N - rollback_allowed_milestones release. For example,
  // if the current version is 70 and rollback_allowed_milestones is 4, this
  // will contain the key versions of version 66. This is used to ensure that
  // the kernel and firmware keys are at most those of v66 so that v66 can be
  // rolled back to.
  RollbackKeyVersion past_rollback_key_version;
};
static_assert(sizeof(off_t) == 8, "off_t not 64 bit");

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_OMAHA_RESPONSE_H_
