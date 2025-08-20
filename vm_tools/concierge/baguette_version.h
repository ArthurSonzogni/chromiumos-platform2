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
constexpr char kBaguetteVersion[] = "2025-08-20-000101_0b4ff8096004f76b00fa494de80e5b0e25fd4e32";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "f9f06a83c3b7dfc31d5523913d05dd79a4289cf10e24d58763fb785b83b5e93a";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "29f5412408fbb41a750ba81f5cdcb071f68d115c25f33b615d09a97dfb5c1fce";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
