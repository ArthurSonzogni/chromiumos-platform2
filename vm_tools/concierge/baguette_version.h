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
constexpr char kBaguetteVersion[] = "2026-08-14-000113_68eaac7f87828c28c47702a12c3379036562cbca";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "ade3122e3bebd048918898396bd3474fdfd1b61ef5790b50b29a480711423ebe";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "4d1fc2d8ef14bc7020939a45a1bab0c9f1a70a39bb2c7b00be8a8b3ddba97ebd";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
