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
constexpr char kBaguetteVersion[] = "2026-07-20-000212_d4891fabfe7ee28ab15c590b0e87e78e77a06af0";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "1c183e314d74626bee26b77cc9326ea3f3899123e4a866057114a8887e06131d";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "3e1b0fc1b8d4e59b280df44ad28d7ee77a9f236d4d9fe13518ee540b61a09344";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
