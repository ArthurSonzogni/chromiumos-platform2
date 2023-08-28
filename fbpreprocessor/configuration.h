// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_CONFIGURATION_H_
#define FBPREPROCESSOR_CONFIGURATION_H_

namespace fbpreprocessor {

class Configuration {
 public:
  static constexpr int kDefaultExpirationSeconds = 1800;

  int default_expiration_secs() const { return default_expiration_secs_; }

  void set_default_expirations_secs(int seconds) {
    default_expiration_secs_ = seconds;
  }

 private:
  int default_expiration_secs_ = kDefaultExpirationSeconds;
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_CONFIGURATION_H_
