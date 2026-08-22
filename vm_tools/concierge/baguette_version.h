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
constexpr char kBaguetteVersion[] = "2026-08-22-000118_3050786c3520dc1be3caaa2917b3c0bb99c78fed";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "f863a3c4f55398fee035023296af5668c2936e22c08cd283562306f69c865139";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "b52ec676556047f582f2c9a96d2eff47c6e9aeefa488cf7a70df011a89ed546c";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
