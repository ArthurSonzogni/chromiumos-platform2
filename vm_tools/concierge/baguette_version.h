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
constexpr char kBaguetteVersion[] = "2026-06-13-000121_56554efa99492cfc308510679e434a66db8e9be0";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "7f2bce01be6391a70e3fa40db1c9d5a308e32e20dd2dec806b2c41d12624e539";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "8a3b57ec183d017a72b81446d36fa78222e25cf097667857af2f8e8097c62196";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
