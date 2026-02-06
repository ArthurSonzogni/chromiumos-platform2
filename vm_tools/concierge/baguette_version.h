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
constexpr char kBaguetteVersion[] = "2026-02-06-000113_799ee689b6d0265305f6f04400b7b4420bcd39d2";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "37f9f91f703eeb640aeb3a286be71417fd0733d099e4879af55fb4dfee09dc94";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "6f4b711ad8e15580d27b6f4c46327c813bd08ce48b4ff140b295a87066163123";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
