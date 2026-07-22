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
constexpr char kBaguetteVersion[] = "2026-07-22-000106_3a04d5905fc6919afdf5aa76617345fc20c8dbeb";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "66c5a712d0baccf5e50361b698e60f71130a93f2717f271d406166082d3f31c0";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "94f97192f4708abc1b24f8032ccf0e20c3a26185e9c6e38c38aa749cc9204620";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
