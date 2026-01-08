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
constexpr char kBaguetteVersion[] = "2026-01-08-000120_9439dde0ee9dff5224d129413b21009115f6e27c";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "fe1a7861145aba3a9c0bf5b78ab3b05fda8af4ca16994290fa8c60e99295be7e";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "9014dbd33cd40e2ca364d83b79d41df2688d090f6ed9d04b024286c889016c26";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
