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

#include "update_engine/cros/omaha_parser_data.h"

namespace chromeos_update_engine {

// |daystart| attributes.
const char kAttrElapsedDays[] = "elapsed_days";
const char kAttrElapsedSeconds[] = "elapsed_seconds";

// |app| attributes.
const char kAttrAppId[] = "appid";
const char kAttrCohort[] = "cohort";
const char kAttrCohortHint[] = "cohorthint";
const char kAttrCohortName[] = "cohortname";

// |url| attributes.
const char kAttrCodeBase[] = "codebase";

// |manifest| attributes.
const char kAttrVersion[] = "version";

// |updatecheck| attributes.
// Deprecated: "eol"
const char kAttrEolDate[] = "_eol_date";
const char kAttrRollback[] = "_rollback";
const char kAttrFirmwareVersion[] = "_firmware_version";
const char kAttrKernelVersion[] = "_kernel_version";
const char kAttrStatus[] = "status";
// Disables sending the device market segment. Only valid for platform app.
const char kAttrDisableMarketSegment[] = "_disable_dms";
const char kAttrInvalidateLastUpdate[] = "_invalidate_last_update";
const char kAttrNoUpdateReason[] = "_no_update_reason";

// |package| attributes.
const char kAttrFp[] = "fp";
const char kAttrHashSha256[] = "hash_sha256";
// Deprecated: "hash"; Although we still need to pass it from the server for
// backward compatibility.
const char kAttrName[] = "name";
const char kAttrSize[] = "size";

// |postinstall| attributes.
const char kAttrDeadline[] = "deadline";
const char kAttrDisableHashChecks[] = "DisableHashChecks";
const char kAttrDisableP2PForDownloading[] = "DisableP2PForDownloading";
const char kAttrDisableP2PForSharing[] = "DisableP2PForSharing";
const char kAttrDisablePayloadBackoff[] = "DisablePayloadBackoff";
const char kAttrDisableRepeatedUpdates[] = "DisableRepeatedUpdates";
const char kAttrEvent[] = "event";
// Deprecated: "IsDelta"
const char kAttrIsDeltaPayload[] = "IsDeltaPayload";
const char kAttrMaxFailureCountPerUrl[] = "MaxFailureCountPerUrl";
const char kAttrMaxDaysToScatter[] = "MaxDaysToScatter";
// Deprecated: "ManifestSignatureRsa"
// Deprecated: "ManifestSize"
const char kAttrMetadataSignatureRsa[] = "MetadataSignatureRsa";
const char kAttrMetadataSize[] = "MetadataSize";
const char kAttrMoreInfo[] = "MoreInfo";
const char kAttrNoUpdate[] = "noupdate";
// Deprecated: "NeedsAdmin"
const char kAttrPollInterval[] = "PollInterval";
const char kAttrPowerwash[] = "Powerwash";
const char kAttrPrompt[] = "Prompt";
const char kAttrPublicKeyRsa[] = "PublicKeyRsa";
// Deprecated: "sha256"; Although we still need to pass it from the server for
// backward compatibility.
// |postinstall| values.
const char kValPostInstall[] = "postinstall";
const char kValNoUpdate[] = "noupdate";

}  // namespace chromeos_update_engine
