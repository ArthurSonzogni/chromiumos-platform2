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
constexpr char kBaguetteVersion[] = "2026-09-26-000110_ba00d8db5e01625a7bfb4c6ac76d0b42d54f495f";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "6c0305527ff66f810aa4d24f3bb9aca24209c306c525c71089dd62485acd7eae";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "093c44188a5741073cf27a8fb5dd15cdbc9d3541d64254c48cbb21d3a0bd06a5";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
