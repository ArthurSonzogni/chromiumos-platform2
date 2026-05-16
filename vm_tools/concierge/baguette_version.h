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
constexpr char kBaguetteVersion[] = "2026-05-16-000107_caa42c8704b9890b0dfc33c6e1b0cb484639a0c2";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "bfcff77e0da4c3be6b11762af906e84aa1f0cf5f6e6d9d9c4b6d0b532bee2295";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "6bee24e8955aaaed94a2349b38bb65f202759f18fe21b027382358966f195d9b";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
