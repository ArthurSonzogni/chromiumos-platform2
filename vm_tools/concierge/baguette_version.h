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
constexpr char kBaguetteVersion[] = "2026-03-25-000436_b6ff5242c631c82b3ab6cbb0ac800d5c7bd5219e";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "2e44e90952c74992f5ed4612e2c67ba947ec34e7d668357e33df777e6182264c";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "fe1647a5d71a37965b12df775750591ecb704168876cc680d5014562f9bc6274";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
