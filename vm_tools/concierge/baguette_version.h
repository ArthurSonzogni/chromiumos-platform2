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
constexpr char kBaguetteVersion[] = "2025-06-01-000128_a153a2fc29b18865ba8d2c58d931a91d331da605";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "6eef2a9ac2efe014f957d061e8c74bc700279dac366ca0fcdd119861ff7f57d6";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "220d7cbbe792d522c9b41452e99c1c9f5f4256f9224689b8506d5e503f419571";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
