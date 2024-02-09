// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_BOOT_CONTROL_H_
#define UPDATE_ENGINE_COMMON_BOOT_CONTROL_H_

#include <memory>

#include "update_engine/common/boot_control_interface.h"

namespace chromeos_update_engine {
namespace boot_control {

// The real BootControlInterface is platform-specific. This factory function
// creates a new BootControlInterface instance for the current platform. If
// this fails nullptr is returned.
std::unique_ptr<BootControlInterface> CreateBootControl();

}  // namespace boot_control
}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_BOOT_CONTROL_H_
