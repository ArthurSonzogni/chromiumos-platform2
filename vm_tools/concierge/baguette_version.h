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
constexpr char kBaguetteVersion[] = "2026-02-15-000126_77f0a5524e0818d88332ea28eead3e3361c698b6";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "03862d85a74e3218b07b12842beceb161805f6fc6b712f7a06ad3fffb9397d6b";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "cc04c3d6338d046669917f4ff1d0ad850677082935265064ecac9fbb6773a084";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
