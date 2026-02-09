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
constexpr char kBaguetteVersion[] = "2026-02-09-000132_13d795dfb5e4edf19e42d13c3ff27045d578412b";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "f697de13a7d236d6dc57a6827bd5e5903c4768c94f5053b0088578456776fbca";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "710d3e3668c8517d5a752d18373f44e32bed83dd58a73a7130c8a7584ffa6a4f";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
