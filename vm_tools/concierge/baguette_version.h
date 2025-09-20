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
constexpr char kBaguetteVersion[] = "2025-09-20-000105_eadd59ded15ffcd65838990b72e263d5cf0256ef";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "c3e9d7d819d26e30882c3461136b7a6ef5ca4cd509a44fc94e2d859e4df2c2e7";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "f43afa011696af40c1359f0fdddab4347e3552bed3da679ce00d5eaf42d8b550";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
