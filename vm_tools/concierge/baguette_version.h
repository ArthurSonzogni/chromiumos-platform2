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
constexpr char kBaguetteVersion[] = "2025-06-21-000101_79ece58191e466d11cc511ac8bdf81af097d07c3";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "4b7cb9041f9655588b1c63dbb1eba0a6f2e42ab3a1a7a8a2b6517b3d5e0c1a23";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "9dd34f5f97197f88e8dc1e38b072b210b08f2d966c7a1660fe7fd4fb63c89203";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
