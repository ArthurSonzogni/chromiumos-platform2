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
constexpr char kBaguetteVersion[] = "2026-10-20-000102_abdc90929bef208ced7eae78c244cb46f906867d";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "51053ee0153306e4947b0960e346f6a58711dd88ed365a13f0ee4a2b3d362430";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "cfa0d88e07d31067993a82e09504e5471f8338050b281a99c6f412705acbea1b";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
