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
constexpr char kBaguetteVersion[] = "2026-06-04-000117_c13f0fd651651fa832027d4775eec212605e80fd";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "96d8bd59e2983e26d24d31e1661d3f32913f42e6e236dd1ec0cb5d6afb5c4489";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "8fdd9b4f7bb80fe5a1db17f102bd56927dff0b79cc7068746b61705778e261a7";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
