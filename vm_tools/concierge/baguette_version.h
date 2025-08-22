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
constexpr char kBaguetteVersion[] = "2025-08-22-000058_8233a45d80384a280614129f79f641b42d1c48c4";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "1e8f842192dff3e96bd70a4c1f0804801c5ec7ca512bb7115f41e66563e9780c";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "307087452978d60df46f32153abf3c876eddce1230f8de037b9bafd8e9c25869";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
