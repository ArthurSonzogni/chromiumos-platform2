// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_FUZZERS_FUZZED_USER_STATE_H_
#define U2FD_FUZZERS_FUZZED_USER_STATE_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <base/optional.h>
#include <brillo/secure_blob.h>
#include <fuzzer/FuzzedDataProvider.h>

#include "u2fd/user_state.h"

namespace u2f {

class FuzzedUserState : public UserState {
 public:
  explicit FuzzedUserState(FuzzedDataProvider* data_provider);

  // Generates the next state with the fuzzed data
  void NextState();

  // UserState methods
  base::Optional<brillo::SecureBlob> GetUserSecret() override;
  base::Optional<std::vector<uint8_t>> GetCounter() override;
  bool IncrementCounter() override;

  // Not implemented UserState methods
  void SetSessionStartedCallback(
      base::RepeatingCallback<void(const std::string&)> callback) override;
  void SetSessionStoppedCallback(
      base::RepeatingCallback<void()> callback) override;
  bool HasUser() override;
  base::Optional<std::string> GetUser() override;
  base::Optional<std::string> GetSanitizedUser() override;

 private:
  void NoImplementation() { LOG(FATAL) << "Method not implemented"; }

  FuzzedDataProvider* const data_provider_;

  base::Optional<brillo::SecureBlob> user_secret_;
  base::Optional<uint32_t> counter_;
};

}  // namespace u2f

#endif  // U2FD_FUZZERS_FUZZED_USER_STATE_H_
