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
constexpr char kBaguetteVersion[] = "2026-08-19-000109_1c5cb1715850da8072400fdb3f74b30ae96c1e64";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "de969dcc6a71a636074d6baa8eaa682bdbcb000b22fcfd42f8417d799a5bdf20";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "80b24a923f8a6eb155fcda9236dcf67800f098960368d806de026fe116e28575";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
