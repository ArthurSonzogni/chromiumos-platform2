// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
#define VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

// This constant points to the image downloaded for new installations of
// Baguette.
// TODO(crbug.com/393151776): Point to luci recipe and builders that update this
// URL when new images are available.

// clang-format off
constexpr char kBaguetteVersion[] = "2025-11-29-000132_836e73c6fc0693ba71d061e5f2f951a9569ae932";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "003ef6a996b21714608482a74f5f80d5a941277100b0a160ca0c3b3bd457998f";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "8069b49ab3336dd0ed03568396f5196d952b1e82dfd9dede09018fcaae81adfb";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
