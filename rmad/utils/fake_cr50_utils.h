// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_FAKE_CR50_UTILS_H_
#define RMAD_UTILS_FAKE_CR50_UTILS_H_

#include "rmad/utils/cr50_utils.h"

#include <string>

#include <base/files/file_path.h>

namespace rmad {
namespace fake {

class FakeCr50Utils : public Cr50Utils {
 public:
  explicit FakeCr50Utils(const base::FilePath working_dir_path);
  ~FakeCr50Utils() override = default;

  bool GetRsuChallengeCode(std::string* challenge_code) const override;
  bool PerformRsu(const std::string& unlock_code) const override;
  bool EnableFactoryMode() const override;
  bool DisableFactoryMode() const override;
  bool IsFactoryModeEnabled() const override;

 private:
  base::FilePath working_dir_path_;
};

}  // namespace fake
}  // namespace rmad

#endif  // RMAD_UTILS_FAKE_CR50_UTILS_H_
