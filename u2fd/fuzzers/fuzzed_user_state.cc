// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/fuzzers/fuzzed_user_state.h"

#include <base/sys_byteorder.h>

#include "u2fd/util.h"

namespace u2f {

FuzzedUserState::FuzzedUserState(FuzzedDataProvider* data_provider)
    : data_provider_(data_provider) {
  NextState();
}

void FuzzedUserState::NextState() {
  // This function consumes the same amount of data regardless of whether or not
  // the state is nullopt.

  std::string user_secret =
      data_provider_->ConsumeBytesAsString(kUserSecretSizeBytes);
  if (data_provider_->ConsumeBool()) {
    user_secret_ = brillo::SecureBlob(user_secret);
  } else {
    user_secret_ = base::nullopt;
  }

  uint32_t counter = data_provider_->ConsumeIntegral<uint32_t>();
  if (data_provider_->ConsumeBool()) {
    counter_ = counter;
  } else {
    counter_ = base::nullopt;
  }
}

base::Optional<brillo::SecureBlob> FuzzedUserState::GetUserSecret() {
  return user_secret_;
}

base::Optional<std::vector<uint8_t>> FuzzedUserState::GetCounter() {
  if (!counter_.has_value()) {
    return base::nullopt;
  }

  std::vector<uint8_t> counter_bytes;
  util::AppendToVector(base::HostToNet32(*counter_), &counter_bytes);
  return counter_bytes;
}

bool FuzzedUserState::IncrementCounter() {
  (*counter_)++;

  return data_provider_->ConsumeBool();
}

// Not implemented UserState methods
void FuzzedUserState::SetSessionStartedCallback(
    base::RepeatingCallback<void(const std::string&)>) {
  NoImplementation();
}
void FuzzedUserState::SetSessionStoppedCallback(
    base::RepeatingCallback<void()>) {
  NoImplementation();
}
bool FuzzedUserState::HasUser() {
  NoImplementation();
  return false;
}
base::Optional<std::string> FuzzedUserState::GetUser() {
  NoImplementation();
  return base::nullopt;
}
base::Optional<std::string> FuzzedUserState::GetSanitizedUser() {
  NoImplementation();
  return base::nullopt;
}

}  // namespace u2f
