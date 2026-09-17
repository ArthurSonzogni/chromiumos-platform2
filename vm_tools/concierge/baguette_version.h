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
constexpr char kBaguetteVersion[] = "2026-09-17-000114_61c3d2ad52d0446991aff163e535e6b5e0d3070d";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "6313835be7a5bcb6755ed4c3ec84561521016232b90ec364e8e7178806704d39";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "81b0447e2e7686fc62de44b54c2a61b273fc30d2a5cb1f0cbeb3de05625e4e55";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
