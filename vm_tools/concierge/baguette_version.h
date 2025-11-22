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
constexpr char kBaguetteVersion[] = "2025-11-22-000059_dd41da8edf84f4a8f8ab69b6189c622155f9c74b";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "2d1be2a0eb4e7badb208b757bb166deb3c3a0758a6ef404538ce5aba03aee1ab";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "146d3fb2fbf9ce9688bb9472416c7fc0b07f1d275b4f88d4914c2c47bd18a87b";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
