// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_PROTO_PRIORITY_NAME_H_
#define MISSIVE_PROTO_PRIORITY_NAME_H_

#include <string>

namespace reporting {

// Temporary replacement for `Priority_Name` that does
// not work in certain CQ.
// TODO(b/294756107): Remove this function once fixed.
std::string Priority_Name_Substitute(int priority);

}  // namespace reporting

#endif  // MISSIVE_PROTO_PRIORITY_NAME_H_
