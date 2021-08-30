// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_VPD_UTILS_H_
#define RMAD_UTILS_VPD_UTILS_H_

#include <string>

namespace rmad {

class VpdUtils {
 public:
  VpdUtils() = default;
  virtual ~VpdUtils() = default;

  // Set a (key, value) pair to RO VPD. Return true if successfully set the
  // value, false if fail to set the value.
  virtual bool SetRoVpd(const std::string& key, const std::string& value) = 0;

  // Get the value associated with key `key` in RO VPD, and store it to `value`.
  // Return true if successfully get the value, false if fail to get the value.
  virtual bool GetRoVpd(const std::string& key, std::string* value) const = 0;

  // Set a (key, value) pair to RW VPD. Return true if successfully set the
  // value, false if fail to set the value.
  virtual bool SetRwVpd(const std::string& key, const std::string& value) = 0;

  // Get the value associated with key `key` in RW VPD, and store it to `value`.
  // Return true if successfully get the value, false if fail to get the value.
  virtual bool GetRwVpd(const std::string& key, std::string* value) const = 0;
};

}  // namespace rmad

#endif  // RMAD_UTILS_VPD_UTILS_H_
