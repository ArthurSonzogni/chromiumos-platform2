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
constexpr char kBaguetteVersion[] = "2025-10-31-000416_f7b41de462d26c6b0c35ee9d5c8bb2ae32c1045d";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "338c4f3f6b8d4121253cb2197b83506ec4f3644f3a07c8edd5c790e6a2bf7795";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "9b060504c39ced7f3a58ac9d3b30d92150f417972916c0254bdb0a3bb563246a";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
