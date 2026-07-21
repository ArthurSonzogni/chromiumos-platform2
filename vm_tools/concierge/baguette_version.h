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
constexpr char kBaguetteVersion[] = "2026-07-21-000145_27e23d99e74a5a89ba1612e04a1e64b681c001b0";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "33343e8da3a0cac60b59e5e3d3f6494ccd14b2f397034fa34e05f73aeab4c53b";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "bbcc6e0e795ea555c6e18c558c70150633ce3d8103a8c7754e21f98e013dd39b";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
