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
constexpr char kBaguetteVersion[] = "2026-01-15-000107_2d8cff03cbb5288b497b26663f2a5253fb71b30c";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "f1bcd343fff6a2acaab657108a8dba4755353dd3f9fd56bd2cbe0affb12d5941";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "dd9a959efacabb83f64e17593dbd24d5df7681e32aa615735441a70ad66cfc10";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
