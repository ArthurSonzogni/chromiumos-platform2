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
constexpr char kBaguetteVersion[] = "2025-06-10-000059_2bc5cd2f0ce704a61d888f178013a15bf6fe83c8";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "65c991e9ad9f359d4f7551c4d4274f5be0e742a7ecaf4f1f21d744065a0d11f0";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "b01cfc0ad7c255b15889fa9169fc0ac58361bf86c0f5a22768fb19973455f0f3";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
