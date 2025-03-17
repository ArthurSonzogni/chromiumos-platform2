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
constexpr char kBaguetteVersion[] = "2025-01-29-000057_6310e875487f154a58648db8fb3cc284401f856e";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "e21336031b00057afd4f3414369cbf98d8e12783cb38a98cd12f7b9318bdc443";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "ab60ff3fc717c575aba8a26cd0b2b113ce29781a2e298d484f6e420a87416aec";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
