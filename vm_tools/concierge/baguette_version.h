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
constexpr char kBaguetteVersion[] = "2026-04-28-000419_e4c45760499562812ae128ff9d90b558e4139054";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "df01f7f7550accdda6e87599248d7d26286db7fb90202cfd574a017c449a05f4";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "d99b347c84ea6a8517ee6e9101acd84f1836f9d06530a6c4f5982b95b8bdc542";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
