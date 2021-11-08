// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_FAKE_HARDWARE_VERIFIER_CLIENT_H_
#define RMAD_SYSTEM_FAKE_HARDWARE_VERIFIER_CLIENT_H_

#include "rmad/system/hardware_verifier_client.h"

#include <base/files/file_path.h>

#include "rmad/proto_bindings/rmad.pb.h"

namespace rmad {
namespace fake {

// A fake |HardwareVerifierClient| that always returns the same verification
// result.
class FakeHardwareVerifierClient : public HardwareVerifierClient {
 public:
  explicit FakeHardwareVerifierClient(const base::FilePath& working_dir_path);
  ~FakeHardwareVerifierClient() override = default;

  bool GetHardwareVerificationResult(
      HardwareVerificationResult* result) const override;

 private:
  base::FilePath working_dir_path_;
};

}  // namespace fake
}  // namespace rmad

#endif  // RMAD_SYSTEM_FAKE_HARDWARE_VERIFIER_CLIENT_H_
