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
constexpr char kBaguetteVersion[] = "2026-05-26-000104_365847e7d3fbb69c7dc50d865205923a30458f11";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "6dc0a1bec6b4aa65b3c32b3b22cbb9e80377d74a51de97eb22efd32a16397226";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "45579dfa206829b77202a568bab7a2446d4e352c9af0b9054cd3082389c8fd39";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
