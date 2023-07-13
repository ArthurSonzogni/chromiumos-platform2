// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/segmentation/fake_segmentation_utils.h"

#include <base/files/file_path.h>

#include "rmad/constants.h"
#include "rmad/utils/json_store.h"

namespace {

// Input JSON keys.
constexpr char kIsFeatureEnabledKey[] = "is_feature_enabled";
constexpr char kIsFeatureMutableKey[] = "is_feature_mutable";
constexpr char kFeatureLevelKey[] = "feature_level";
// Output JSON keys.
constexpr char kIsChassisBrandedKey[] = "is_chassis_branded";
constexpr char kHwComplianceVersionKey[] = "hw_compliance_version";

}  // namespace

namespace rmad {

FakeSegmentationUtils::FakeSegmentationUtils(
    const base::FilePath& working_dir_path)
    : working_dir_path_(working_dir_path),
      is_feature_enabled_(false),
      is_feature_mutable_(false),
      feature_level_(0) {
  base::FilePath input_file_path =
      working_dir_path_.AppendASCII(kFakeFeaturesInputFilePath);
  auto input_dict = base::MakeRefCounted<JsonStore>(input_file_path, true);
  if (input_dict->Initialized()) {
    // Read JSON success. The keys might not exist, and in that case the
    // variables remain the default values.
    input_dict->GetValue(kIsFeatureEnabledKey, &is_feature_enabled_);
    input_dict->GetValue(kIsFeatureMutableKey, &is_feature_mutable_);
    input_dict->GetValue(kFeatureLevelKey, &feature_level_);
  }
}

bool FakeSegmentationUtils::GetFeatureFlags(bool* is_chassis_branded,
                                            int* hw_compliance_version) const {
  // This fake class doesn't support this function.
  return false;
}

bool FakeSegmentationUtils::SetFeatureFlags(bool is_chassis_branded,
                                            int hw_compliance_version) {
  base::FilePath output_file_path =
      working_dir_path_.AppendASCII(kFakeFeaturesOutputFilePath);
  auto output_dict = base::MakeRefCounted<JsonStore>(output_file_path, false);
  if (!output_dict->Initialized()) {
    return false;
  }
  output_dict->Clear();
  output_dict->SetValue(kIsChassisBrandedKey, is_chassis_branded);
  output_dict->SetValue(kHwComplianceVersionKey, hw_compliance_version);
  return output_dict->Sync();
}

}  // namespace rmad
