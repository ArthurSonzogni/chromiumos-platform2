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
constexpr char kBaguetteVersion[] = "2025-05-03-000059_a0c8af651c81249aece85367564215bcda4c869c";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "003e95db95afc83a8e18081574fc52171f5ce54b61c2997944e0156f0494780c";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "0efba19defa852d673a5fd4db149efc968d5b28821d08e0dc64143a21c3ad72b";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
