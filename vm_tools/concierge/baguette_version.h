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
constexpr char kBaguetteVersion[] = "2025-08-11-000122_8e62efaf24c0e81a8a40ae08e4939902688229fe";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "f3f98837413e5c1de4c64dbade970ec694610ecd8b2454bddeed1123d6312477";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "842526a39d4d09a64a5d2cccb5566ca811fd28489a201af0a5125481373cd7bf";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
