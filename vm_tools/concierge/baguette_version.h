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
constexpr char kBaguetteVersion[] = "2025-08-29-000059_28f5a48ce1773d1bdab85c9d1d6dc21eecf32577";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "625e5dbdd6d2083460457ec2781ad0070d94e58de433b0a0456961f62736d7a2";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "938356404afb935c7ace9daca26f42117d4978fba22a270b045d227aef72ea09";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
