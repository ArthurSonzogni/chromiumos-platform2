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
constexpr char kBaguetteVersion[] = "2026-04-17-000106_4066289276dc06d2c4691476fe654c38bf4b729e";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "cbb3ea3396307fa17eaa2c98869408ae58993626be7332ca0afb0d9adf36b60a";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "5026f0e436e78b5cd171bb5abb31896e4d64e13000ec9abf690e45beb1526eab";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
