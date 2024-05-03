// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGMON_FEATURES_REGMON_FEATURES_H_
#define REGMON_FEATURES_REGMON_FEATURES_H_

namespace regmon::features {

class RegmonFeatures {
 public:
  RegmonFeatures(const RegmonFeatures&) = delete;
  RegmonFeatures& operator=(const RegmonFeatures&) = delete;
  virtual ~RegmonFeatures() = default;

  virtual bool PolicyMonitoringEnabled() = 0;

 protected:
  RegmonFeatures() = default;
};

}  // namespace regmon::features

#endif  // REGMON_FEATURES_REGMON_FEATURES_H_
