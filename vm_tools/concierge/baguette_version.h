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
constexpr char kBaguetteVersion[] = "2025-11-24-000117_51ddf0efbe0a1cb3b78949f398895c29faca9b30";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "6e03823358b9c887aeba2ded1a468fb8a834d5478f0e019f86aecf26250b407f";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "bef48f03ae2b56a1ad877d21ecd7c76fe5cdee3c64541a305fa160a14f974ed3";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
