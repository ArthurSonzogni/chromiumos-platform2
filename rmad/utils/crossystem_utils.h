// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_CROSSYSTEM_UTILS_H_
#define RMAD_UTILS_CROSSYSTEM_UTILS_H_

#include <string>

namespace rmad {

class CrosSystemUtils {
 public:
  CrosSystemUtils() = default;
  virtual ~CrosSystemUtils() = default;

  // Set a (key, value) pair with int value to crossystem. Return true if
  // successfully set the pair, false if fail to set the pair.
  virtual bool SetInt(const std::string& key, int value) = 0;

  // Get the int value associated with the key `key` in crossystem, and store it
  // to `value`. If the key is not found, `value` is not modified by the
  // function. Return true if successfully get the value, false if fail to get
  // the value.
  virtual bool GetInt(const std::string& key, int* value) const = 0;

  // Set a (key, value) pair with string value to crossystem. Return true if
  // successfully set the pair, false if fail to set the pair.
  virtual bool SetString(const std::string& key, const std::string& value) = 0;

  // Get the string value associated with the key `key` in crossystem, and store
  // it to `value`. If the key is not found, `value` is not modified by the
  // function. Return true if successfully get the value, false if fail to get
  // the value.
  virtual bool GetString(const std::string& key, std::string* value) const = 0;
};

}  // namespace rmad

#endif  // RMAD_UTILS_CROSSYSTEM_UTILS_H_
