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
constexpr char kBaguetteVersion[] = "2026-07-23-000137_c0f93487075799fc9cf9218789172a1a44568baa";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "386ff0a8a7ba7e6d00f7ef1b77f8412b5f3145913a177413fb74deed8174e013";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "c6a824bca44acf6613f5d06943202a130ac86824487e66b43b5dd417ddec6744";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
