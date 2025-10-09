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
constexpr char kBaguetteVersion[] = "2025-10-09-000059_bed0c57c204ac86a2a0424f5bc2660290433a3dc";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "d0e932e7ebbf7d8833b5287b7f441878c7d743709960fc2437e688dfc834120c";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "1df54c8329bf869325178224a1e92c8b06758945cb380a75509afaa58eb1a71c";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
