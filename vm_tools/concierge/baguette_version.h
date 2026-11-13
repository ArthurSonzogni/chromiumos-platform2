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
constexpr char kBaguetteVersion[] = "2026-11-13-000127_4bebdf7e5be29d5fd3e155da4767e07cf583586e";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "473d8f42b3f6307204ab978ffd8c7a9181813b39bc615308b539413087b283bd";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "4d3af870f94c01403ed68fe0583df97b0bbba8d204f605ee5195abcc9003affe";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
