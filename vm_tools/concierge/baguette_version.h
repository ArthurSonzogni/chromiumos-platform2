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
constexpr char kBaguetteVersion[] = "2025-07-03-000110_e88eb334ae6bfbefbf74e9e771e350f997254ef3";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "8f1731de3a0afa8e29d71a505ae10f6925b3966fedf6c3bf14d89acde73796b3";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "af5a1f024885c787f4482ba6036b090e75b18f1defe41ffe3f84530e84e452b2";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
