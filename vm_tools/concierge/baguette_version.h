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
constexpr char kBaguetteVersion[] = "2026-08-07-000135_21daa5b0ad5ba0fb3c8be86cab7638fb7a52a488";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "bcd221e2ae653d9253e4b60a6125a0ffbe25e74238de8dcc188acc6a939c4570";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "7568183ddab2008cff65cea32d2cdbc7405b6528cf18e8566b89bac18db557e7";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
