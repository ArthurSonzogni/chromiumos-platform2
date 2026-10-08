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
constexpr char kBaguetteVersion[] = "2026-10-08-000107_aff0a17df840eb8da99f7c44f1310335296eb9b2";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "12bd9e6d1c0437c4c14d5929b3187b4b113f27f8c8e2f9c6b785a6a5b4641e4e";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "89e5df48c443213c7ad8926c0de2c675e5e84715d7660a44024239de8ad37684";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
