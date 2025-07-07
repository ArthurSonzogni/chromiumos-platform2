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
constexpr char kBaguetteVersion[] = "2025-07-07-000141_0c832334c0127954743f71cadce1bc1a0efb491c";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "66070d77dfc07051be2a1f7a6a87b87c6aaf1ebf6fb6826fd1dabdfd35c902c2";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "8c3af67776223824125ac69098b6b03e24e4dee758371f7fade4ceeda963fb2c";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
