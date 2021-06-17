// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_CR50_UTILS_H_
#define RMAD_UTILS_CR50_UTILS_H_

#include <string>

namespace rmad {

class Cr50Utils {
 public:
  Cr50Utils() = default;
  virtual ~Cr50Utils() = default;

  // Returns true if the RO verification key is triggered on the current boot,
  // false if the key is not triggered.
  virtual bool RoVerificationKeyPressed() const = 0;

  // Get the RSU challenge code. Return true if successfully get the challenge
  // code, false if failed to get the challenge code.
  virtual bool GetRsuChallengeCode(std::string* challenge_code) const = 0;

  // Use the unlock code to perform RSU. Return true if the unlock code is
  // correct, false if the unlock code is rejected.
  virtual bool PerformRsu(const std::string& unlock_code) const = 0;

  // Check if cr50 factory mode is enabled.
  virtual bool IsFactoryModeEnabled() const = 0;
};

}  // namespace rmad

#endif  // RMAD_UTILS_CR50_UTILS_H_
