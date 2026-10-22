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
constexpr char kBaguetteVersion[] = "2026-10-22-000121_5fe8c7334ea87e9d166f0df3d11c00498b9360ff";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "43f8663352ddace7e1e0565e2826f8438592ea7ec68e11a12c3fdcaf319032c1";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "bf363af8d973b3aa277e798a1091fbd4cd984f584aa14b58b1436928e92d78b7";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
