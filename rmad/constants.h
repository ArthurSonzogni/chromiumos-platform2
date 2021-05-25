// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_CONSTANTS_H_
#define RMAD_CONSTANTS_H_

namespace rmad {

// JsonStore rmad_interface keys.
constexpr char kStateHistory[] = "state_history";
constexpr char kStateMap[] = "state_map";
constexpr char kNetworkConnected[] = "network_connected";
constexpr char kSameOwner[] = "same_owner";

}  // namespace rmad

#endif  // RMAD_CONSTANTS_H_
