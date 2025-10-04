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
constexpr char kBaguetteVersion[] = "2025-10-04-000100_c13934b45f5a583e7768723fd97485a655b6b112";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "d07555b8e5b35dc568a14bfe9948e2a238d040c49c7d5f5c80cc09a2aabc7755";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "465a409aa02bc41ad4d5261062195ff66d913893f993dbf530eebd0b260f47cf";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
