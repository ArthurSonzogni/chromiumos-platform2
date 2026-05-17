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
constexpr char kBaguetteVersion[] = "2026-05-17-000138_af21537508de1982970078dc09564ca9c3fda677";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "b5075ffbadb1a784669cbfe6308414d6100a1fea2da53358f58fc52024b09856";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "332d0a4e0bf6c1a40b11f75edfa855e00e8fb01ffff65dab859c348d8622896f";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
