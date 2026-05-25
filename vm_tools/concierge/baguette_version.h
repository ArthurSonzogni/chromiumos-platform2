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
constexpr char kBaguetteVersion[] = "2026-05-25-000125_90760aae5f6066fecd8382b41cf8e1669cf7a292";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "5d5a04bf7241f2345d0f788d88a835f206bca4c3388b6dbe2d0c665bcc5be425";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "c8ad52615c90a3e30155fb005fb6e9dc44a3de61405b12875074b0f77d047b9e";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
