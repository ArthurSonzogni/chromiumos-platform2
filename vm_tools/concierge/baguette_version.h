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
constexpr char kBaguetteVersion[] = "2026-04-04-000101_284b637bd5401e90c1c7c493e92e4f81748b75cd";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "a7c2ec05af639d038d40bdebe6ef0821ccb4a5310d953c98ed0ce73b84012d71";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "dff9238ac09b77e54c3df59e3be638952fe52312096a772201ac95148318ca8e";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
