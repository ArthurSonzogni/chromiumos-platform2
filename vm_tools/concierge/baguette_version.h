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
constexpr char kBaguetteVersion[] = "2025-08-09-000105_bfd4a27cfffe0b0c83299ed0828e3ff58d3cbe9f";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "e778afeafb287bcc4a896e80838310406e678868c18022a05ecd0367b6582010";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "eeab243cd405d36a1bf5bef65306ede7bf47f75ca437a4fc6c1bfb95f58f4317";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
