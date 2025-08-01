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
constexpr char kBaguetteVersion[] = "2025-08-01-000109_d398b0aec11f2ef0927e6403da3714a3a6ff5462";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "1d21cae5252827dc826861ddcc06fcc846a0db0fe6a42deb00f2cb3a635c2980";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "781431388913a71222438ba442d1a4f15dc540d017692ccddae2c9ddf3cbe748";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
