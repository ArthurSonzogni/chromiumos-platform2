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
constexpr char kBaguetteVersion[] = "2026-10-29-000111_2df0b466fdcb054ed365d3efcec40285029a6cf8";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "3f19557ac57e418df70551ad2f008b81b0d9197b4dc94d6067c65dc79119c9f4";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "48ee9542ca06c75ec6e136f05002b6cad0f8f704b2f3766fd6874e5e9cd300db";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
