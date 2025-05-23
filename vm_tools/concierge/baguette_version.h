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
constexpr char kBaguetteVersion[] = "2025-05-23-000123_3b8e1eb0a6bfab6b525488a6eef1adc0582ab674";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "683f7fb1d28dc5ad5296a2dc1f3cd4cc11ef95039aff2b3663657ad6bc7c1c47";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "f171fd8716e51cc3a3e72c2ed7c66f80a394ca1de617290babf78af7ec3c13c4";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
