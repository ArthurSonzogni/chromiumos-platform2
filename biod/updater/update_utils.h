// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_UPDATER_UPDATE_UTILS_H_
#define BIOD_UPDATER_UPDATE_UTILS_H_

#include <string>

#include <base/files/file_path.h>

#include "biod/biod_system.h"

namespace biod {
namespace updater {

std::string UpdaterVersion();

// Checks for external firmware disable mechanism.
bool UpdateDisallowed(const BiodSystem& system);

}  // namespace updater
}  // namespace biod

#endif  // BIOD_UPDATER_UPDATE_UTILS_H_
