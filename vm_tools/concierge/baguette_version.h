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
constexpr char kBaguetteVersion[] = "2026-06-24-000106_82604baca99c4163a90a26ec5db16f3da23ffdef";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "b9fb46398f53541a69e2d1a7774338fcf619c7d4ba19355a4672ddfec19b409c";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "6b5bf0472dd4b722480dedbd1d9feb743df09e79cfa0c4209fa103975d0e4a4a";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
