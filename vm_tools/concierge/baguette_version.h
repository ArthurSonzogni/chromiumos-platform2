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
constexpr char kBaguetteVersion[] = "2025-11-20-000109_2fb2420ff448cd681caf6cc7fd559929ffa8a1da";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "9aa604ad465a48e1ee963103fe520dae30e2d6bb58927c58fbd11acc60b00922";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "94ac74531defd6abcc4e933750902263fedd181f06417dbdc26ae29bdcce53c2";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
