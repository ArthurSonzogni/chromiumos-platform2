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
constexpr char kBaguetteVersion[] = "2025-11-13-000107_026756e9cc0d790144a2b37235bbabaafa8eb934";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "b23639e9bbb13199bb75c8d7022fbabbc2efe3ff53b8e8d6ba7783db37d91767";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "dd133b0858413fed6a6ca949a58fe46206aca8850448db30e515d7a10ca3111e";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
