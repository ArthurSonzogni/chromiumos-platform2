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
constexpr char kBaguetteVersion[] = "2025-11-12-000106_3091718b27e591efe546e8284f374b7c85d97d2e";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "5672aa0888e8dc3145399197c8744b2d66592f30a0697a6b9c1f5bc0ac4913c4";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "76ede546031d799b467b54677ce40f6e00f9a249cf68ecb97bac3b1fd3ffea25";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
