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
constexpr char kBaguetteVersion[] = "2025-06-03-000058_a695ccadbf6427e3a006fdb3456c6f443e5b1489";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "10c56907fdbb3ea4df906ff3b98bb1a4287a55c74a3977269e007f472c812137";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "ff53dc1b6013b570e7aaa3cc8b89fd7dc61858451ce7b1ef31767bee2ca391e3";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
