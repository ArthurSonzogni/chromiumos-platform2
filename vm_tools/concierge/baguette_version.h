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
constexpr char kBaguetteVersion[] = "2025-07-31-000103_f5addf9b54e06cbe5e345d9619fca964f9660f96";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "660f9589dea80eae2e86531b70f4ee9837adb2c01544332d5cd01a36308ede19";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "d631c623941d0964b10bd348002c2b8263ee2bfc1b4ec9c2b787b0c4db60304d";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
