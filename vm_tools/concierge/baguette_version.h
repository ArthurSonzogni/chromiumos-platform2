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
constexpr char kBaguetteVersion[] = "2025-06-07-000106_012d1f1c11637f1a051912bd9118c4b72c2eff84";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "7adffe2f5095ed9d7128f0b509b8576ed86c3af24b0ce933c0abac7c11f12e5a";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "edcb6115ad5b3196453c6a858ac6cc706dc852fcb62265579b61f63dabb75a23";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
