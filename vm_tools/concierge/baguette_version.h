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
constexpr char kBaguetteVersion[] = "2026-04-09-000107_22a0f8f24a821e5de2c6ca6034d35158540b6d71";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "edfb7c7b4a667ee8bc1d9e135d385d31de03ead04a392879a510e73e2674ae67";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "4ba3ccbc424464a2e5a6831c7f4075073375e931d99d4464d334576be9db1929";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
