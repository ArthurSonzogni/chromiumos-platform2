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
constexpr char kBaguetteVersion[] = "2026-11-21-000120_c2ccff7855f036ebc1e5872aba43a76c0a233ad7";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "26097fea1c21cc4e34cf89090abf6fea0f6b9236ca3679d1c2a1517de6ae8200";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "adbe4dcf7f17ecf84fecdddccaf8439851ef9b4211306ed0e6ae2d163dc212d2";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
