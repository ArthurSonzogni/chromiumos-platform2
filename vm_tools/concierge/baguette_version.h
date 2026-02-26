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
constexpr char kBaguetteVersion[] = "2026-02-26-000112_3b18fc015bacbea969f2c5d48af376d26b21ac8f";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "c8515f07bbe0b4cc0588bd831a9c83d2a5cc297d51c361978e96d4e651cbf730";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "0adf496d4e0a0da687d31b65771bfe803e23743474be33c2d97dec4aacbdb8ff";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
