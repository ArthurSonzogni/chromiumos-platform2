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
constexpr char kBaguetteVersion[] = "2026-06-17-000108_f6a8588d36ec4ad33d204cd4110e30fe8fdc0cd5";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "77c115926675759feb28e6d9cd1050c669ea5ee33c676f12e609bcdd8f82ba43";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "54f38f665b78074620ed69d20eba9a6a67b82fd5dd4fba94f197d0fb4f1bbfb4";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
