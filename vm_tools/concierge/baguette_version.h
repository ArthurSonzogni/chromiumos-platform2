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
constexpr char kBaguetteVersion[] = "2026-03-04-000126_f9bbe7201b6de4f350758a2df2d5953a7b414142";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "ecb87479a49f9b4b43d83ce0edd8576fa6d59fef90e8b5d43eab1136eea7bfa3";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "8f82abec3ee8b506bdd555c13427de3a9c6cd8aa585ab5bab3bf9afea1e53594";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
