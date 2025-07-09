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
constexpr char kBaguetteVersion[] = "2025-07-09-000103_24e407e98572cda12507409171de2c50572381bd";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "859a936b5fcad694ee4939b31ee332b3d93421d7dfce85623d17c614b243d794";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "0d9b0eb0e02bb3c44e681c5dd75b7b323521b4ec1e12a5d9ec0345ed38f38b4d";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
