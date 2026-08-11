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
constexpr char kBaguetteVersion[] = "2026-08-11-000129_029032b04170a7b469494833c6e0215cccf5f196";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "18872739acf257c8ae263bd64f55808b6392b30cc03c6480eec4cd36f737d2a4";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "94401d6602513943efe45bfefb6d23eda39f88faf8d34626442d97ee30640aa9";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
