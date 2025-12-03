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
constexpr char kBaguetteVersion[] = "2025-12-03-000058_de63a0c52eeaf9c2b39da2aef466de15e39a45f6";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "83c3505b4fce73d41e81a4c04db76d15e8ffb7721a6868b39709df274939574f";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "7c8e8e98013e36f407b51bc8dc4dbef8399d4b2e4fe8a1a4bc8f806397e9dc78";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
