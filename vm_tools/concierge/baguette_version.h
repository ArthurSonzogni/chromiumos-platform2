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
constexpr char kBaguetteVersion[] = "2025-09-02-000106_ab905a61e5a463281c95a59618a8d4e39a8adbe1";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "54180f32f0f47df4af0ea8a5b395b38d5f4bd5911ee497493eaa12fbb8617c5e";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "e99f031c352feae47145131fab6af1ab5ac3aa52ac30738ec24f29ecf501b199";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
