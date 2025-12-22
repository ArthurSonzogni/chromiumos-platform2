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
constexpr char kBaguetteVersion[] = "2025-12-22-000136_3b09c9acc732ce0f82810cf8d00c3838375eb467";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "9d34f0d4def84c5b91c15e3280f4098806e8af6d76d0137d971e0576bc1e53bc";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "12e737404ed51523182dfc79d806855366e4e0b909898b12afa6795920518a77";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
