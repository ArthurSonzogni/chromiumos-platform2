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
constexpr char kBaguetteVersion[] = "2025-08-04-000124_6f26720635c1663c6ce3a68a072f2464baf06189";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "b0c7b4ead65ba2355af8f7a62334af3121d374591f4f9fef8a1b2334f561bbb3";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "8d1e4e3fe766566f94f61582ad5716b3cdcee855b5b79245150682bbc6e491de";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
