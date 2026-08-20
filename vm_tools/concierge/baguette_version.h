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
constexpr char kBaguetteVersion[] = "2026-08-20-000126_cb48ad1471763ce5d1a45e36dd5a1d96b7867218";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "4126349849ca9a6de58a9b69a2fcaaca3944cb293f2bc5ee7b4d102401ed877a";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "599f2afc32d6b543c94a72459e53c835d55c469272f176ca56a7a81b4503c183";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
