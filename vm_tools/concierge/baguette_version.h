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
constexpr char kBaguetteVersion[] = "2026-09-20-000110_5a2009835642e9a0e198e0094cfae9537515386b";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "0f53e7de4400f1f594f15d2e7e30e5222d5d8c4064469d664dc8218f0996717b";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "8dad2224f56897f48ca857343799f6365684081e72e111942e852902ec64aa39";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
