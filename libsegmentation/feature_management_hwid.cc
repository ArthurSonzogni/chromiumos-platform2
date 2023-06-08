// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include <base/logging.h>

#include "libsegmentation/device_info.pb.h"
#include "libsegmentation/feature_management_hwid.h"
#include "libsegmentation/feature_management_interface.h"
#include "libsegmentation/feature_management_util.h"

namespace segmentation {

using chromiumos::feature_management::api::software::DeviceSelection;
using chromiumos::feature_management::api::software::SelectionBundle;

std::optional<DeviceSelection> FeatureManagementHwid::GetSelectionFromHWID(
    const SelectionBundle& selection_bundle_,
    const std::string& user_readable_hwid,
    bool check_prefix_only) {
  const std::optional<std::string> hwid =
      FeatureManagementUtil::DecodeHWID(user_readable_hwid);
  if (!hwid)
    return std::nullopt;
  for (const auto& selection : selection_bundle_.selections()) {
    for (const auto& hwid_profile : selection.hwid_profiles()) {
      // Look into the database for a prefix match.
      bool prefix_match = false;
      for (const auto& prefix : hwid_profile.prefixes()) {
        if (user_readable_hwid.rfind(prefix, 0) == 0) {
          prefix_match = true;
          break;
        }
      }
      if (!prefix_match)
        continue;
      if (check_prefix_only)
        return selection;

      bool all_requirement_met = true;
      for (const auto& requirement : hwid_profile.encoding_requirements()) {
        std::string bit_value = "";
        for (auto bit_location : requirement.bit_locations()) {
          bit_value.append(1, bit_location < hwid.value().size()
                                  ? hwid.value()[bit_location]
                                  : '0');
        }
        bool pattern_found = false;
        for (auto required_value : requirement.required_values()) {
          if (bit_value == required_value) {
            pattern_found = true;
            break;
          }
        }
        if (!pattern_found) {
          all_requirement_met = false;
          break;
        }
      }
      if (all_requirement_met) {
        return selection;
      }
    }
  }
  return std::nullopt;
}

libsegmentation::DeviceInfo_FeatureLevel HwComplianceVersionToFeatureLevel(
    int32_t hw_compliance_version) {
  switch (hw_compliance_version) {
    case 0:
      return libsegmentation::DeviceInfo_FeatureLevel::
          DeviceInfo_FeatureLevel_FEATURE_LEVEL_0;
    case 1:
      return libsegmentation::DeviceInfo_FeatureLevel::
          DeviceInfo_FeatureLevel_FEATURE_LEVEL_1;
    default:
      return libsegmentation::DeviceInfo_FeatureLevel::
          DeviceInfo_FeatureLevel_FEATURE_LEVEL_UNKNOWN;
  }
}

libsegmentation::DeviceInfo_ScopeLevel HwComplianceVersionToScopeLevel(
    bool is_chassis_x_branded) {
  if (is_chassis_x_branded)
    return libsegmentation::DeviceInfo_ScopeLevel::
        DeviceInfo_ScopeLevel_SCOPE_LEVEL_1;

  return libsegmentation::DeviceInfo_ScopeLevel::
      DeviceInfo_ScopeLevel_SCOPE_LEVEL_0;
}

libsegmentation::DeviceInfo FeatureManagementHwid::GetDeviceInfo(
    FeatureManagementHwid::GetDeviceSelectionFn get_selection,
    bool is_chassis_x_branded,
    int32_t hw_compliance_version) {
  libsegmentation::DeviceInfo device_info_result;

  // Implementing decision tree from go/cros-tiering-dd.
  if (is_chassis_x_branded || hw_compliance_version > 0) {
    if (is_chassis_x_branded) {
      device_info_result.set_feature_level(
          HwComplianceVersionToFeatureLevel(hw_compliance_version));
      device_info_result.set_scope_level(HwComplianceVersionToScopeLevel(true));
    } else {
      if (get_selection(false)) {
        device_info_result.set_feature_level(
            HwComplianceVersionToFeatureLevel(hw_compliance_version));
      } else {
        device_info_result.set_feature_level(
            libsegmentation::DeviceInfo_FeatureLevel::
                DeviceInfo_FeatureLevel_FEATURE_LEVEL_0);
      }
      device_info_result.set_scope_level(
          HwComplianceVersionToScopeLevel(false));
    }
  } else {
    std::optional<DeviceSelection> selection = get_selection(true);
    if (selection) {
      device_info_result.set_feature_level(
          HwComplianceVersionToFeatureLevel(selection->feature_level()));
    } else {
      device_info_result.set_feature_level(
          libsegmentation::DeviceInfo_FeatureLevel::
              DeviceInfo_FeatureLevel_FEATURE_LEVEL_0);
    }
    device_info_result.set_scope_level(HwComplianceVersionToScopeLevel(false));
  }
  return device_info_result;
}

}  // namespace segmentation
