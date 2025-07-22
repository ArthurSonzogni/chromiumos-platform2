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
constexpr char kBaguetteVersion[] = "2025-07-22-000114_d1ee039226305ad9bb1c5bc5ac737cb1eeb3275d";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "86fc76ee96d5799c78d050492bcb79da20e564f05350c31a0f746592e15760b5";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "134a8c1a81699f157fb8fa931c8d6c6443ced35875484492b4a51ce742a39b61";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
