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
constexpr char kBaguetteVersion[] = "2026-05-03-000123_81e0b061e43bc3cf2ed2a46c349c35f450f1c7c4";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "351887e61255f8bcb8ee38448dd77268c2a2eb5c7a37221afac2e23da40181d7";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "6d7dec7bc9f65099d9603e9090aa14edfd99579bcd19530757d75aade3629036";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
