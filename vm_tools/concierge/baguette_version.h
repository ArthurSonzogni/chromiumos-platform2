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
constexpr char kBaguetteVersion[] = "2026-09-19-000129_98b6eeb13775fb3aa6e2f205fd70fd06ab457346";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "888053664bceef01c0125544217b35889297cc057e922c70c379c9808d90da3b";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "64423fa839f6ca2ef1641998c517f4b5576042450caa3bfac6c24a4f7b800b93";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
