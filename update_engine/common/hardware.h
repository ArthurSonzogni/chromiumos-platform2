// Copyright 2013 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_HARDWARE_H_
#define UPDATE_ENGINE_COMMON_HARDWARE_H_

#include <memory>

#include "update_engine/common/hardware_interface.h"

namespace chromeos_update_engine {
namespace hardware {

// The real HardwareInterface is platform-specific. This factory function
// creates a new HardwareInterface instance for the current platform.
std::unique_ptr<HardwareInterface> CreateHardware();

}  // namespace hardware
}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_HARDWARE_H_
