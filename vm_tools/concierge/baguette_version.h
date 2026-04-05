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
constexpr char kBaguetteVersion[] = "2026-04-05-000118_c05961de46cdb8c15efc6ca3bbb32cb6b6178420";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "74e50d4c85fa76c9cc0dfee2dd09c37cf835af718eb72152e23f3c93078ff97b";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "49f86bcb403314a1d291287ee85afd825db668100d0bcb01dc28acbac13f3856";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
