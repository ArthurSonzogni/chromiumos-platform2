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
constexpr char kBaguetteVersion[] = "2025-07-25-000125_7b026b6f5922388ed6301cdf5eade72497c8d075";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "c25952b4a94e1fdf794fdd904a31f254eb7e4eca1bede1bd931653641fbba187";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "726152cfa954d02a6f4b3feb25fc79edb4be96613449273ccbb18c1c5528a642";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
