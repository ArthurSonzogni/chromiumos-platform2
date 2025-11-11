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
constexpr char kBaguetteVersion[] = "2025-11-11-000100_79c76d95b91be86cf3dbb99954cd0d83ca638b6b";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "3e6ba14a176d86bb3600e173b2cafe19478310b6b7a4d6a9e08188c51c1f2ed1";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "a20d7111bd04706dd27f645f0480436ddf68c356c73d0ac48bbca4de4d59c1df";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
