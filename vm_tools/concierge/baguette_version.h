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
constexpr char kBaguetteVersion[] = "2026-11-22-000115_cff046d876a19e3e87ee4b98b1255de1b27aaaf1";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "55fe94282b7440d186e57dce0dee077eb0ec39992dbccb6a86699ec1d5935595";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "b1e18d71d3a071861c571e3597b2e7512441e9b6e2246c9c78e0674892053a8e";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
