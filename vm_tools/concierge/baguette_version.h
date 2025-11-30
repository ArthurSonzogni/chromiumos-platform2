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
constexpr char kBaguetteVersion[] = "2025-11-30-000131_e54f0626d51f2e612a2d4d54e6129dcb0d330536";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "a986152e07019e59a31c1feede8a4ada269cf6fe0bc536694712985cd53df22d";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "c0aeed38a6acd4046329b9ea95be9c51f4af1938f4cc34c5a7fd52f761de48ca";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
