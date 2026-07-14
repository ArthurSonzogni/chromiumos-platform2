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
constexpr char kBaguetteVersion[] = "2026-07-14-000620_86028d105bc1bfd55edcd6d68ade930519451c48";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "a39dd766111b5fd494be7014215e41bd2afdcf6aebd49e1a7a08192a032f4370";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "b832d234758e8d24b130387c4bc3a76a21c006b7dccb70dbdd40cbacb2fef0e5";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
