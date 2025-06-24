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
constexpr char kBaguetteVersion[] = "2025-06-24-000115_8dac9fc5b8cd8b150fc4db30d0e40f6120cff8a1";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "93566a77ef32536226c488939c1a17e82b19f74ded1fdd33a8a2483caa523678";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "f024e0004d10ed7ef9e771c0dc9e7d2d98021c4e9c8d82e827e57bb906bb6307";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
