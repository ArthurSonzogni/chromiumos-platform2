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
constexpr char kBaguetteVersion[] = "2026-09-25-000108_4ef0975cf9419ff26c89f9d8beb14890eeecb93d";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "3ab585b62b463a7e229a52f0ff1a940d44d68a4af5f3875d1880377edb9a2bca";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "bce51a276829b045d759400bf80846276bada9bb2a9edee1873ddaf9597544ad";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
