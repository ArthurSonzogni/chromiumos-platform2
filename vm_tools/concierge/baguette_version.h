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
constexpr char kBaguetteVersion[] = "2026-10-27-000119_3df85e4a91186ba1a002e451aa953b9db54f95d5";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "957d253ad36e41461e1dd596e7c6ce30efde1fdd06b1419671ce4906aa308f9c";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "69fc414fc916faeccd00c6a8a5630f0181bd136ac5984de87d42c4ece1639d6a";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
