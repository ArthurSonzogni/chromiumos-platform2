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
constexpr char kBaguetteVersion[] = "2025-04-18-000101_ffce57c4dceebae645ca0c27d001da3f76a1892d";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "6c4caf8cfa60be54bc0690ffcff324713e943f3604d8c57affdf8bc24a67d877";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "276343c2114b40a7cb8b6d036ed77a6d2f870eb5039765d6e80527b405474999";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
