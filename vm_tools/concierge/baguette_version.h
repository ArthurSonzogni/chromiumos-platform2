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
constexpr char kBaguetteVersion[] = "2025-06-11-000118_f54e41a4cc86e8999bf9750dbea85caff1fb74c4";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "22e346f4d3452fa3eb1607fa6253f24cf5db1536be969f0b31b1f19bf60f26df";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "0669f47521dcedc34de49b0f1e6e99939622257122075aabed41cdb566598e56";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
