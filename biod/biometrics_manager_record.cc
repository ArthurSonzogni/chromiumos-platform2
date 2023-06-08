// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/biometrics_manager_record.h"
#include "biod/cros_fp_biometrics_manager.h"

namespace biod {

const std::string& BiometricsManagerRecord::GetId() const {
  return record_id_;
}

std::string BiometricsManagerRecord::GetUserId() const {
  CHECK(biometrics_manager_);
  const auto record_metadata =
      biometrics_manager_->GetRecordMetadata(record_id_);
  CHECK(record_metadata);

  return record_metadata->user_id;
}

std::string BiometricsManagerRecord::GetLabel() const {
  CHECK(biometrics_manager_);
  const auto record_metadata =
      biometrics_manager_->GetRecordMetadata(record_id_);
  CHECK(record_metadata);

  return record_metadata->label;
}

std::vector<uint8_t> BiometricsManagerRecord::GetValidationVal() const {
  CHECK(biometrics_manager_);
  const auto record_metadata =
      biometrics_manager_->GetRecordMetadata(record_id_);
  CHECK(record_metadata);

  return record_metadata->validation_val;
}

bool BiometricsManagerRecord::SetLabel(std::string label) {
  CHECK(biometrics_manager_);

  auto record_metadata = biometrics_manager_->GetRecordMetadata(record_id_);
  CHECK(record_metadata);

  record_metadata->label = std::move(label);

  return biometrics_manager_->UpdateRecordMetadata(*record_metadata);
}

bool BiometricsManagerRecord::Remove() {
  if (!biometrics_manager_)
    return false;

  return biometrics_manager_->RemoveRecord(record_id_);
}

}  // namespace biod
