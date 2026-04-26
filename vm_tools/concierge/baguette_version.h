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
constexpr char kBaguetteVersion[] = "2026-04-26-000137_5b0b98611e1960460b9aa35b92549fe6b1039d7f";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "9a633757b93be1db7443b81400e58400c953fafb1c53c19e47c76eeb92549f0e";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "c3ad45adfbe0123403dcdcc60b4807fb9aad6a95941e1637da8c3102d731a9d6";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
