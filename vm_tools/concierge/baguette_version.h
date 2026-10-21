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
constexpr char kBaguetteVersion[] = "2026-10-21-000110_79e88eca4542f6af68890b5ee9b14306ffdcd2d9";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "25d8baf6217a90434e6ea432751a4bc14c4b6d6751081a5ebe3729d1c2d84a87";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "14b98ec559742dc258847d5421f8470d0c23b025c03d911d760af348ad6e7c04";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
