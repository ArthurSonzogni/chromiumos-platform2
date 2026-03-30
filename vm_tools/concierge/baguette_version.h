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
constexpr char kBaguetteVersion[] = "2026-03-30-000119_7de875e6c5897e3e18e678bc13ec17c668653f96";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "3e0c3ca3716b3cfaea3a675fa2ef6c8c4d1bc72840708ac0cb3907f782148437";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "8250d140216ad208283b989be3dd15545c12a757c6b86016eb4d8361341a4273";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
