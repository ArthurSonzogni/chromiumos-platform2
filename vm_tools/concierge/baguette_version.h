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
constexpr char kBaguetteVersion[] = "2025-12-17-000101_6ae6a220794a98b58f2d6bd9cf9a7020f064fd59";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "b8fa04564fa34f4ad4ad5dc1162aca7cebbb86611a7e186dd31a5a644c3a730f";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "e856725065fef76b31c77f55350eb272715aa9921c07f8bf6098c67506125f3a";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
