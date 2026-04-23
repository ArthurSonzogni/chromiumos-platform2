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
constexpr char kBaguetteVersion[] = "2026-04-23-000118_cf713e2ef00f7d2f0e9a053a8956dbbca59d741c";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "d003cc609024fda84e45a92919baa171f19a350f5ed8a4281607d255bed16c80";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "7d624e1079f499473763f139e250901ae768f920a6bd07127d3f3d3bfacb4cb0";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
