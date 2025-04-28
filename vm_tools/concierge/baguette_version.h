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
constexpr char kBaguetteVersion[] = "2025-04-28-000118_9935be271634f5690babfb3284dc140739799d25";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "941e50dc3d481b36fcbe844d01280ad9649c3d36c2ecf13edb3f734d7f8d0719";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "50e58885f88572992ab1528c9273b70ef62c2e1558dcc7c02367a8d1beb4c599";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
