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
constexpr char kBaguetteVersion[] = "2026-11-27-000133_5c074a44f3b177a8fb4e8ad4b053fed047cb3f61";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "b01d319b13eb1f427b4dc2ef4bbc5737580ff299b59b6550fe9279199d089a46";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "262178634dd8c245fd0c84857cd2521c709b909097845cc5febebc3fd9d6a89f";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
