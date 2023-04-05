// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_UTIL_DYNAMIC_FLAG_H_
#define MISSIVE_UTIL_DYNAMIC_FLAG_H_

#include <atomic>
#include <string>

#include <base/strings/string_piece.h>

namespace reporting {

// Class represents an atomic boolean flag.
// The flag is initialized and then can be queried and/or updated.
// Can be subclassed or aggregated by the owner class.
class DynamicFlag {
 public:
  DynamicFlag(base::StringPiece name, bool is_enabled);
  DynamicFlag(const DynamicFlag&) = delete;
  DynamicFlag& operator=(const DynamicFlag&) = delete;
  virtual ~DynamicFlag();

  bool is_enabled() const;

  void OnEnableUpdate(bool is_enabled);

 private:
  const std::string name_;
  std::atomic<bool> is_enabled_;
};
}  // namespace reporting

#endif  // MISSIVE_UTIL_DYNAMIC_FLAG_H_
