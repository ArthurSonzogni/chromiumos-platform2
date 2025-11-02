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
constexpr char kBaguetteVersion[] = "2025-11-02-000111_1e4774f45b456fe00843279a5338852bd480b410";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "78f16f694a0babdb3a81d7ee852ff3a39fc60ed583845b4212eb02c34e2844da";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "fbcdd941d8d1e970ec5f9ec505185f60a3176e3e800a48449350cd4ec06393b3";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
