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
constexpr char kBaguetteVersion[] = "2025-10-01-000059_2cb4da1e267895a7e96ee2e6b34bf9a7f4aff5d4";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "e400778fc3cbfab1106ce5fdd27647f8f2df30f230065cb570db30b538b3f0b3";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "16caa7562847f432dddd006d234082e3dacd1dad00e28b4aefe9a67334eaa35c";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
