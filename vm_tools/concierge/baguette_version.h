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
constexpr char kBaguetteVersion[] = "2025-04-21-000145_568732173e5275a5a7d479c58b0bf2829d1692cd";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "eb6e07b1501574839d6487959574d535cfab6dc6af32ab1563fdc2560bad3fa3";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "a11e2a5465e200cb4edceb86076ed46d58e1632ecaa144e4308212bd270595ea";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
