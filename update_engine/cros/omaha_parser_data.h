//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef UPDATE_ENGINE_CROS_OMAHA_PARSER_DATA_H_
#define UPDATE_ENGINE_CROS_OMAHA_PARSER_DATA_H_

#include <optional>
#include <string>
#include <vector>

namespace chromeos_update_engine {

// |daystart| attributes.
extern const char kAttrElapsedDays[];
extern const char kAttrElapsedSeconds[];

// |app| attributes.
extern const char kAttrAppId[];
extern const char kAttrCohort[];
extern const char kAttrCohortHint[];
extern const char kAttrCohortName[];

// |url| attributes.
extern const char kAttrCodeBase[];

// |manifest| attributes.
extern const char kAttrVersion[];

// |updatecheck| attributes.
extern const char kAttrEolDate[];
extern const char kAttrRollback[];
extern const char kAttrFirmwareVersion[];
extern const char kAttrKernelVersion[];
extern const char kAttrStatus[];
extern const char kAttrDisableMarketSegment[];
extern const char kAttrInvalidateLastUpdate[];
extern const char kAttrNoUpdateReason[];

// |package| attributes.
extern const char kAttrFp[];
extern const char kAttrHashSha256[];
extern const char kAttrName[];
extern const char kAttrSize[];

// |postinstall| attributes.
extern const char kAttrDeadline[];
extern const char kAttrDisableHashChecks[];
extern const char kAttrDisableP2PForDownloading[];
extern const char kAttrDisableP2PForSharing[];
extern const char kAttrDisablePayloadBackoff[];
extern const char kAttrDisableRepeatedUpdates[];
extern const char kAttrEvent[];
extern const char kAttrIsDeltaPayload[];
extern const char kAttrMaxFailureCountPerUrl[];
extern const char kAttrMaxDaysToScatter[];
extern const char kAttrMetadataSignatureRsa[];
extern const char kAttrMetadataSize[];
extern const char kAttrMoreInfo[];
extern const char kAttrNoUpdate[];
extern const char kAttrPollInterval[];
extern const char kAttrPowerwash[];
extern const char kAttrPrompt[];
extern const char kAttrPublicKeyRsa[];
// |postinstall| values.
extern const char kValPostInstall[];
extern const char kValNoUpdate[];

// Struct used for holding data obtained when parsing the Omaha response.
struct OmahaParserData {
  struct DayStart {
    std::string elapsed_days;
    std::string elapsed_seconds;
  } daystart;

  struct App {
    std::string id;
    std::optional<std::string> cohort;
    std::optional<std::string> cohorthint;
    std::optional<std::string> cohortname;

    struct Url {
      std::string codebase;
    };
    std::vector<Url> urls;

    struct Manifest {
      std::string version;
    } manifest;

    struct UpdateCheck {
      std::string status;
      std::string poll_interval;
      std::string eol_date;
      std::string rollback;
      std::string firmware_version;
      std::string kernel_version;
      std::string past_firmware_version;
      std::string past_kernel_version;
      std::string disable_market_segment;
      std::string invalidate_last_update;
      std::string no_update_reason;
    } updatecheck;

    struct PostInstallAction {
      std::vector<std::string> is_delta_payloads;
      std::vector<std::string> metadata_signature_rsas;
      std::vector<std::string> metadata_sizes;
      std::string max_days_to_scatter;
      std::string no_update;
      std::string more_info_url;
      std::string prompt;
      std::string deadline;
      std::string disable_p2p_for_downloading;
      std::string disable_p2p_for_sharing;
      std::string public_key_rsa;
      std::string max_failure_count_per_url;
      std::string disable_payload_backoff;
      std::string powerwash_required;
      std::string disable_hash_checks;
      std::string disable_repeated_updates;
    };
    std::optional<PostInstallAction> postinstall_action;

    struct Package {
      std::string name;
      std::string size;
      std::string hash;
      std::string fp;
    };
    std::vector<Package> packages;
  };
  std::vector<App> apps;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_OMAHA_PARSER_DATA_H_
