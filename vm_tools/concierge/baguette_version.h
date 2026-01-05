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
constexpr char kBaguetteVersion[] = "2026-01-05-000128_206b5f3138703830b4d75ba2e8f422699500a6ec";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "fdcea7c6f26722dfcd69441520eadda60edb9a5214608510caacf7da83c05af4";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "2d74afccba4f9632640e8f4364c6622fdd9b43b2cb5a79697e306947bc932aed";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
