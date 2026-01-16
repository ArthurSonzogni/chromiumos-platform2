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
constexpr char kBaguetteVersion[] = "2026-01-16-000130_b16c7a5212ead3cb5a3e05adda4cbfb8e4fbc384";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "46a9fc064098bb4e39813d7209e2925f5860c796e98072dcd296048396c9bf34";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "ae178352f34926ad817de3c2623d6ec686c04897a95157ea59d6e7e0284f5bf1";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
