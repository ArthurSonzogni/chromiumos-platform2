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
constexpr char kBaguetteVersion[] = "2026-03-15-000126_69f35fdc381a163b74fea688625f9eceb8bf5287";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "6cf85bcc2918640628407166a25a84dbdce9ed7ed2d061b4cd52955b4f5c4b1c";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "eb1c0a6e011e89fe7c662a013d6cf7497ce5d941657251d794871a05d17d9089";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
