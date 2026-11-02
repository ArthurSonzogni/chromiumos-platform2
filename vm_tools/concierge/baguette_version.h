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
constexpr char kBaguetteVersion[] = "2026-11-02-000113_d3403441f351c9639056d971fd0f3b845d7629cb";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "711c91c75b6decb33f4a9ff315bc54dc8c95e4f33d620007a5dccc4578332421";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "497b6b0464203dfc6f2bf8df4e8564c2181ca1d95bf7c56e59812e5a9dc1ab2f";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
