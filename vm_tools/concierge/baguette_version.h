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
constexpr char kBaguetteVersion[] = "2026-03-13-000057_f3408a1755577011389bc0e1aaf3a2bc72c60421";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "ff421fbf2afa13603b4c3920d4df84120647a6d1682172351722cdda9529d709";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "d1b7538c853b01db7e316fd58038b84ba5b0fea5e34c65feed5239b3cf87d9b5";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
