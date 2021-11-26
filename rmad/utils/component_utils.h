// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_COMPONENT_UTILS_H_
#define RMAD_UTILS_COMPONENT_UTILS_H_

#include <string>

#include <hardware_verifier/hardware_verifier.pb.h>

namespace rmad {

std::string GetComponentIdentifier(
    const hardware_verifier::ComponentInfo& info);

}  // namespace rmad

#endif  // RMAD_UTILS_COMPONENT_UTILS_H_
