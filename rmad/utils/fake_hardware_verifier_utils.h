// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_FAKE_HARDWARE_VERIFIER_UTILS_H_
#define RMAD_UTILS_FAKE_HARDWARE_VERIFIER_UTILS_H_

#include "rmad/utils/hardware_verifier_utils.h"

#include <base/files/file_path.h>

#include "rmad/proto_bindings/rmad.pb.h"

namespace rmad {
namespace fake {

// A fake |HardwareVerifierUtils| that always returns the same verification
// result.
class FakeHardwareVerifierUtils : public HardwareVerifierUtils {
 public:
  explicit FakeHardwareVerifierUtils(const base::FilePath& working_dir_path);
  ~FakeHardwareVerifierUtils() override = default;

  bool GetHardwareVerificationResult(
      HardwareVerificationResult* result) const override;

 private:
  base::FilePath working_dir_path_;
};

}  // namespace fake
}  // namespace rmad

#endif  // RMAD_UTILS_FAKE_HARDWARE_VERIFIER_UTILS_H_
