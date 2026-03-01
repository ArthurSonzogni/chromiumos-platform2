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
constexpr char kBaguetteVersion[] = "2026-03-01-000132_e6df3a96463c0987983c4a75d78c2936ba9d5482";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "2c58c7ad808783533bce954034ded5959413bcfd4434b13205cd6f846a49f6ae";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "74a3d4b1945f514455393f3aa689e8d929a88a10881966e3c678255d91effa58";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
