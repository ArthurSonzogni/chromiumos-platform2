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
constexpr char kBaguetteVersion[] = "2026-09-13-000109_0f2dce5920fb0c68ef671fa756de2821012c8585";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "5a273719667a95c6810bd79d6e81852b4e55bb8a8b1132c5dcf3f262bc35051d";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "9d0ff247a8459030590015d1dc0d6878b67c05102ae2a4d12a5673276b3be02b";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
