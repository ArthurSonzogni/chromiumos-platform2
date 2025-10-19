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
constexpr char kBaguetteVersion[] = "2025-10-19-000107_249bb63364b68d93287a998c33112bafc2e1cd8f";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "e81fdb4974402a184620cec816ef688502ed98b97aecd6dad3cbadcbbfa2d527";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "80323930a5808af48b2fd2477353551308ebf7109cbcf50a4ebfb2a1fd5f811d";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
