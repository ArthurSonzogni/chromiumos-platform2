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
constexpr char kBaguetteVersion[] = "2025-10-17-000116_6ddeb2d6be76231ab124109e92bed8aff315b125";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "843973d355fbd9e5fb10c1c131e80a8759883e22e6f80d4b219e28a38ba8084d";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "326d51e09b7e88c2f1db319184a1c6ea83ba9cd895f4d598401064a9bc04e072";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
