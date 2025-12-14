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
constexpr char kBaguetteVersion[] = "2025-12-14-000120_9b9747993e86fab9fe7fadda8ecccf16f0c65340";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "34ee3048413c950d38d76a7fd2bbab47e7d8e91c5d0ab5e37fd3606de53d9727";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "2cd997e0e33e016c629f004c13b3ec0834f014beaa833abd52a075a5a231758c";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
