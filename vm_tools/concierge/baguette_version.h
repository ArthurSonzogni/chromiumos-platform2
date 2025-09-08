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
constexpr char kBaguetteVersion[] = "2025-09-08-000125_fb4303724dc7474791a5e9b71ec4504e8954e6d1";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "c1be05109e69913a67af63704d3f0a30352e98548fd90403d355c4dab300887c";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "09e18cf46d66ab724ff2e2062f690913a52a38a778a2b26412fbfe2224b67800";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
