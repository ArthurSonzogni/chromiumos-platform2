// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CROS_EXCLUDER_CHROMEOS_H_
#define UPDATE_ENGINE_CROS_EXCLUDER_CHROMEOS_H_

#include <string>

#include "update_engine/common/excluder_interface.h"
#include "update_engine/common/prefs_interface.h"

namespace chromeos_update_engine {

class SystemState;

// The Chrome OS implementation of the |ExcluderInterface|.
class ExcluderChromeOS : public ExcluderInterface {
 public:
  ExcluderChromeOS() = default;
  ~ExcluderChromeOS() = default;

  // |ExcluderInterface| overrides.
  bool Exclude(const std::string& name) override;
  bool IsExcluded(const std::string& name) override;
  bool Reset() override;

  // Not copyable or movable.
  ExcluderChromeOS(const ExcluderChromeOS&) = delete;
  ExcluderChromeOS& operator=(const ExcluderChromeOS&) = delete;
  ExcluderChromeOS(ExcluderChromeOS&&) = delete;
  ExcluderChromeOS& operator=(ExcluderChromeOS&&) = delete;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_EXCLUDER_CHROMEOS_H_
