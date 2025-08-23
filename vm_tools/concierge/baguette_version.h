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
constexpr char kBaguetteVersion[] = "2025-08-23-000106_10f68b545dd05f893e0e36e71e5b1e4682df3d13";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "f723b506a7cebe3591a1f013b59b377c8e5cf0275c37cc387fde003c5437f637";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "98931157cc60018abb65e57302b65ff93ad12f4136b4f6850509953e319e7aa5";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
