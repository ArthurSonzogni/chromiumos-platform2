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
constexpr char kBaguetteVersion[] = "2025-05-01-000138_ca4d40fb2288e0a9c4c5167a19c5df9f4bf0512f";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "01edb0ac7a3d50ff2749060ce7d1f8d39549dbba90c0bd26c6cfc2b008f79aa6";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "39128e168e3dbfd5bba6d9bf248c85f278cee68f959ebc3cc4925ccd7c1608fd";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
