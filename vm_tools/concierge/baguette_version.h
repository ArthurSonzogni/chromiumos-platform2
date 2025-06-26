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
constexpr char kBaguetteVersion[] = "2025-06-26-000129_32e1f8da152cabe1134e996a05a0cf425927bc1a";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "d8fe507974524797693d2c0b252cafd573d9abf16758291a1e890ac67e43ee38";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "cfed52da35e276cd0769c2e0c231c95fe5d73333b97de70d6bb3569e2e9f0a32";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
