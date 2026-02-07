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
constexpr char kBaguetteVersion[] = "2026-02-07-000122_e7ad8bf0e0388c5a2b197c9ec2c228075f861c7d";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "fa562b52ee0afc5e34f61941240bec46456497fb5ddaf13cad28f4ab13905e9c";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "851d170ea3197cd4de58593c4823dcb74b4503ee85c1e74897ec4395dbbf4ddd";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
