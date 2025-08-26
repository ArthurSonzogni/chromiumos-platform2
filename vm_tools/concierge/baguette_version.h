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
constexpr char kBaguetteVersion[] = "2025-08-26-000107_5587b333fd48cee1ffff5f30950c9fecd3c9ac62";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "41794ebaf83f9c1248c72d09776ee572cec9ddac5ac0ebb7ba5df3be99d7930a";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "7b7d238605e1a9bebc986bcd9f11f6096d54899a26e68eb767b29d3157de6627";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
