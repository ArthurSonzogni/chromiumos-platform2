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
constexpr char kBaguetteVersion[] = "2025-04-25-000101_b7cfae937906f17c70e5baff7ce4b7abeb4238f4";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "076ab54725dd8c9970568954f6d6d98c44c1954e00ab11256109d0918ae6988a";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "6202dbcb4d5c1ad577aa08d0a07e4c77b8d4422dc8e807b5913151524dc1a08e";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
