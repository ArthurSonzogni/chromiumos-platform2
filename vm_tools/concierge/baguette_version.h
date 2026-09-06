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
constexpr char kBaguetteVersion[] = "2026-09-06-000150_e08139b36f62a18d436ab45467d5630dc19e6172";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "3f5fcd6e38d586fa691524f583d27a9983eb7971b9e83ac31b51a719f5fc8dfe";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "f9686efbd7d8ea4da560cdb0548f832682f3edac075a291bc8e31f8e1026be8f";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
