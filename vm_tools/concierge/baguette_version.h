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
constexpr char kBaguetteVersion[] = "2025-09-18-000120_f1d5285caac97431f64a32bed682bcca6c516de6";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "bde9c87d527e40ca05f2d37fd3659f6e0ca3ffab0abfe4914cd00fead15e7b58";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "9c43a5f6aa414131db51229844f5fc3085df2cebe477641d2aa91cad070c7cc4";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
