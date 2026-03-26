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
constexpr char kBaguetteVersion[] = "2026-03-26-000105_0265b9c4647a1f2bf87df9e7a0d39041833539b3";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "60298e347e3c5669807006a3b07e2dc9b0286927f06dd4a6bdea6998f2080d92";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "0611c0ae9fcde2f38745cfa4c0105f7b35045c99dca3c1f3a17b104a28dcab8c";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
