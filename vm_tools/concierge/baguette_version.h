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
constexpr char kBaguetteVersion[] = "2026-06-21-000106_6b613ac50aac06960880d45a735ef2f14b62ea2b";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "fa9c56129e6de41aea3ec40964832859193e1353d7bf4be6c21996879730b3d5";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "1a0324815b8cb144537ed546ef12374d99547d31dafec4082e5c6d9596e7ddc0";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
