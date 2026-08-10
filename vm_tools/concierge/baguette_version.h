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
constexpr char kBaguetteVersion[] = "2026-08-10-000115_11866df8e835d3793f0b0d82fe8f8697d1a6eeb2";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "7a912b2d6dcab99222a14a5602959efb915a019c1561647bb34b782c5ee4b38a";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "0a16b824b7d536a4697d47223b1a67512affe48328bddff6ca91d075150d961b";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
