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
constexpr char kBaguetteVersion[] = "2026-04-12-000136_0cbec75e63aa1d00e0219a2f84309b05dae3d0e9";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "7d5291cbb504d9ac203dc5b01cc39e2bcc2394b2496a1aa16a30cf66ded8f0d2";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "17c871e8c56ca982f9ba83211fd8860923d55f23cab1e4197b487b7680b73120";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
