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
constexpr char kBaguetteVersion[] = "2025-04-27-000123_b198c0d5ecb1e61323e8951732fbd6e6bde3635f";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "8c7ea55826205fa3c8dda38b3766f1e7c477be527142e6ce845fd8c0ab0c2f1b";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "e333502ffe90417074bcbbbd126eda5b9b748e7bb74bdd3a2b41a2b048310836";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
