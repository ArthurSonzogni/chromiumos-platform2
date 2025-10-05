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
constexpr char kBaguetteVersion[] = "2025-10-05-000133_4680151d963fc53baa86cd47a851fc7adb006055";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "cb89a9c90e1951af842edc6aeb7e47cffc4d0c2715cd42951f8b0319b082b363";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "19f25bd4aa95fb468e0ba1d2b10c846d78f7e6ecc05fdad43247071d17af749b";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
