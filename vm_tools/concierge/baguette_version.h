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
constexpr char kBaguetteVersion[] = "2025-06-06-000110_3fe5d0fbaff79ff8b0049e9c74014a15f8c63d3e";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "c3de64d232a8f38d193b704dc20cd45db64b83312013bf439c77a15797898954";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "0561ae848a20093140e7735e0901b61ed6f22657db1aa15878514b844226bc84";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
