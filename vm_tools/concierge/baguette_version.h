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
constexpr char kBaguetteVersion[] = "2025-12-11-000112_1f9a51a88e73f5889c121c2886960c6f1c06381b";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "616d169539114ad34352f676a8ee7d7fab1b9388de0e0601b733f1c8c94c8197";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "deaaa403af2756cf5d5d4a1a08bed5fe2f1c8c12fda535647a6612808e08f499";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
