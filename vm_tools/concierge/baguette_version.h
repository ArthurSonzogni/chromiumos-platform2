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
constexpr char kBaguetteVersion[] = "2025-12-16-000108_f8d49e98fbfc12fb09913461bf8f2ba2c53e858b";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "7d46eb09466d5df595200a9a45d870bacec28f7f984e34fd368caf354be8c51e";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "f030db750bce0ccf251f78ab68aab85f418c144671ca6d82dbec847a6b71c46d";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
