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
constexpr char kBaguetteVersion[] = "2025-12-01-000123_745e883c4e8e95f8d096e89920be114576997d64";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "e7f372746e74ea5e2549a2c55ae3fd9116ef442022ca916075201bfd68ced92a";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "6646fd89785895ff952d8f063b0523032342c76c2fd671e09dda69afd5b11c75";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
