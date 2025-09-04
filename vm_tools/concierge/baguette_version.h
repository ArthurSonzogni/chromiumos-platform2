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
constexpr char kBaguetteVersion[] = "2025-09-04-000113_3b356f31f49a0e044c74a0440f13ec3602a45fd5";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "59d92fa95770bdf6486e65c631860a35da6cceec5b1af0fded857c60bcdf44a2";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "5236a0d493d7fc2e01deff70a220e10d3fe86c3f2738d238d5b99e2a9e82a9cf";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
