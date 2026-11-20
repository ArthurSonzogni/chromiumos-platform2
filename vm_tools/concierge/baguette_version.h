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
constexpr char kBaguetteVersion[] = "2026-11-20-000114_0a2c5e576c15adec1a9f718f8f9cd049fabd4177";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "e411b315c53f961482ae79599c17c65c7bc55b554afedcdba590f110d404e960";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "4c091fb73b9046eed5de8d77e2fbfa8073d2c2044f3328c167fb04c97e6e9b2b";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
