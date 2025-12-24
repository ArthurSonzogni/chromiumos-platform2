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
constexpr char kBaguetteVersion[] = "2025-12-24-000117_e22d0d45f53c258ad47599df11583bcdb420f539";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "ec0dcf3e9f794cb20eadd778929a75d25097722ba8ce02b132b8a03778bcd469";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "fa3e03ab72480190ef132a69635c13511a61b6704c4f90cabae2729e1e9d4881";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
