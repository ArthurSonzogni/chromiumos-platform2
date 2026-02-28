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
constexpr char kBaguetteVersion[] = "2026-02-28-000120_d397495ba14888feca631c893b3c065dcf5d4616";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "1ef5178dffe1ebaaca64a4d2d0a3a79fca067928d30d5f589189cb7ca22cf893";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "c53708ebfae275bb4a519762ecdc48e23c3cc164ff32aae55b4ca51a8a743cf9";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
