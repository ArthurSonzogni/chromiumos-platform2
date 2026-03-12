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
constexpr char kBaguetteVersion[] = "2026-03-12-000102_0549f3b2c8977cd9bdc0099f1725b8726f7de7c1";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "58f353cad471eac50ec50afee00a664fd35e6933fb6a9f1bbfc414d45d125cfc";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "92635cebfb5982257e8d672ae13479143dd9b1009c3e992b286f7f8200731c9f";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
