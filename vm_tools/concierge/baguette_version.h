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
constexpr char kBaguetteVersion[] = "2025-07-24-000123_719e8d9e5b12a921538c76e5598f6182e07a57bd";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "44104e76617cd2067f607c26dc6ffee9d6195c79eedafcdb06314caa5a544fb5";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "a1db31c46342bb5883dbe4669a6c63c91ffb9b2d6fcab9d83289397be4a59df1";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
