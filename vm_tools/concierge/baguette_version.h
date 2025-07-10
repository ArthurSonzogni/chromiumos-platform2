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
constexpr char kBaguetteVersion[] = "2025-07-10-000313_89e6e28a2d73e3c073d27b4e4852ec31b61de15e";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "b48d1fdd6bcdf9e49ea9ef765822b2c929c70dcc759a1858356a946a2ed2cb81";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "5b762248305ecc61d0b1804415dfc620ed334d03ce04bdeb4dccff4d2b88e619";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
