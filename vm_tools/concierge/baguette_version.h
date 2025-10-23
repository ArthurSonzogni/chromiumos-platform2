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
constexpr char kBaguetteVersion[] = "2025-10-23-000304_3c354f3a2f396a707067916dd7560329794b1249";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "fea50a6450f7c9054fb8932e8c6346fa248878b8427b902cb25ee5f7b2feca3b";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "12c65d2d17f4621624d42f3666430a6cec92742ebae8d46b20434eae57da4678";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
