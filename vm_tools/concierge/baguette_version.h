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
constexpr char kBaguetteVersion[] = "2026-05-22-000115_441a1c98fc925856a0baa903018b84d71e97458a";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "58895c080e12bb8daab053b1eeb9f496043eff82b38ec1c5817f84f657c2f2c3";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "4d152a72ec3de44dc85d0bafc3a0453bf16b0d307d3cfb8822d452b21ceae0a9";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
