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
constexpr char kBaguetteVersion[] = "2026-01-22-000100_8737ab5cfa677a22875429b32af24f0865d9bae2";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "483016571d1587228742bf022de52e1d0c8f1865ade2b31163c8c22955ecff71";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "3e7b8e3270bdecae6ccead36b0d2252b5e7d9b8a0fea416b7a0629e4d707d777";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
