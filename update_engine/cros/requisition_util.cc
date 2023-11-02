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

#include "update_engine/cros/requisition_util.h"

#include <memory>
#include <vector>

#include <base/files/file_util.h>
#include <base/json/json_file_value_serializer.h>
#include <base/logging.h>
#include <base/strings/string_util.h>

#include "update_engine/common/subprocess.h"
#include "update_engine/common/utils.h"

using std::string;
using std::vector;

namespace {

constexpr char kOemRequisitionKey[] = "oem_device_requisition";
constexpr char kNoRequisition[] = "none";

}  // namespace

namespace chromeos_update_engine {

string ReadDeviceRequisition(const base::Value* local_state) {
  string requisition;
  bool vpd_retval = utils::GetVpdValue(kOemRequisitionKey, &requisition);

  // Check the Local State JSON for the value of the device_requisition key iff:
  // 1. The VPD value is missing as a result of users manually converting
  // non-CfM hardware at enrollment time.
  // OR
  // 2. Requisition value mistakenly set to "none".
  if ((requisition.empty() || requisition == kNoRequisition || !vpd_retval) &&
      (local_state && local_state->is_dict())) {
    auto* path = local_state->GetDict().FindByDottedPath(
        "enrollment.device_requisition");
    if (!path || !path->is_string()) {
      return "";
    }
    requisition = path->GetString();
  }
  return requisition;
}

}  // namespace chromeos_update_engine
