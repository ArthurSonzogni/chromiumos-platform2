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
constexpr char kBaguetteVersion[] = "2025-07-04-000123_0e616eed5d55d6f3eea77b7d4ee1d51d7e750c2d";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "212904754bfb74a34cd646977adbc3ec35571d226e5040469fe5669b973b3c44";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "5a10985ad7ae072a8121dbe14868f85cd57d0101d8429d2f9f18cc04e7970654";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
