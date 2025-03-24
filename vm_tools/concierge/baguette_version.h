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
constexpr char kBaguetteVersion[] = "2025-03-24-000115_105b989ebf0fc11f0144fc03f68863fc0f612ab5";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "a8c1c3f7de15dd71ff5cbad2dda2a6aa7ef11a02d1e4ffbe7ad5b4a8a4d850e6";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "d67847021d3195683db0f98084bf7e2db0b5d048b329c4ab05e08c5fff5d89b7";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
