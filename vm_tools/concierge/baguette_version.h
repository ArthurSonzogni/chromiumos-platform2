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
constexpr char kBaguetteVersion[] = "2025-12-05-000106_fd28962189ad7bcac5a14e2f31ad53971b206c05";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "b36a53f4606724cc54fc8d58251bf7a1e1a07c42cc7d6b6cbb8d7d08978d8bbd";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "29b78fd309cfc4dd21f4f96790a4594711a498a106fa22729e2e1824725f0888";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
