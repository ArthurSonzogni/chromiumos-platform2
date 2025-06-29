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
constexpr char kBaguetteVersion[] = "2025-06-29-000118_9b80d2df75e9312e58e8908dfb8175a06094a1ff";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "055c884e3bc843187069f891a7d619f62442923cc435c90fad8ac6c121db5df2";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "402c199aefb018a0f7f2a75ef7b72cfe8471260201b6204f57b3a891fe8f9a9e";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
