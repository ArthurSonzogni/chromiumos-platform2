// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_BIOMETRICS_MANAGER_RECORD_H_
#define BIOD_BIOMETRICS_MANAGER_RECORD_H_

#include <string>
#include <utility>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "biod/biometrics_manager_record_interface.h"

namespace biod {

// Invokes the function object F with a given BiometricsManager object when
// this session (EnrollSession or AuthSession) object goes out of scope. It's
// possible that this will do nothing in the case that the session has ended
// due to failure/finishing or the BiometricsManager object is no longer
// valid.

class CrosFpBiometricsManager;

class BiometricsManagerRecord : public BiometricsManagerRecordInterface {
 public:
  BiometricsManagerRecord(
      const base::WeakPtr<CrosFpBiometricsManager>& biometrics_manager,
      const std::string& record_id)
      : biometrics_manager_(biometrics_manager), record_id_(record_id) {}

  // BiometricsManager::Record overrides:
  const std::string& GetId() const override;
  std::string GetUserId() const override;
  std::string GetLabel() const override;
  std::vector<uint8_t> GetValidationVal() const override;
  bool SetLabel(std::string label) override;
  bool Remove() override;

 private:
  base::WeakPtr<CrosFpBiometricsManager> biometrics_manager_;
  std::string record_id_;
};

}  // namespace biod

#endif  // BIOD_BIOMETRICS_MANAGER_RECORD_H_
