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
constexpr char kBaguetteVersion[] = "2025-10-16-000108_0758eeb00fb8ea885c87ce14aeaac6ec4a753739";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "28334ec81c14e7c9c92d1ce73124cf8ae57b8ef2f5c818929673fcc6677a17bf";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "1b99cc68b4f8e93aac35a95821ae0c70a1fddd73e943e230a244eba1ec6f757c";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
