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
constexpr char kBaguetteVersion[] = "2025-11-08-000119_647536d7a10cff00424d2f439261d26896484079";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "9bcf806e53418e4f8c54a2632f032a9304cd0b907f6716c59de78ae5bf7f2d5a";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "050faae8780f883de9ad9769854969e959d58f8992b49f1c2d2e959fb91f4180";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
