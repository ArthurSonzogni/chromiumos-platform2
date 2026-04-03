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
constexpr char kBaguetteVersion[] = "2026-04-03-000105_3bc56325f40687f071405920b0ade9f7ec430915";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "84a46b51b9c2608926b560aa1d27a804b17aa7ce5893dd6ee6b32acce2e50799";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "67d33f0096323175e0e042a7a0d70b00cc8a63b8922a4b3b98d5cc2f5d8f2c11";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
