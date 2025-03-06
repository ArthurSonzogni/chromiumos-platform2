// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
#define VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

// This constant points to the image downloaded for new installations of
// Baguette.
// TODO(crbug.com/393151776): Point to luci recipe and builders that update this
// URL when new images are available.
constexpr char kBaguetteVersion[] =
    "2025-01-29-000057_6310e875487f154a58648db8fb3cc284401f856e";
constexpr char kBaguetteSHA256[] =
    "ab60ff3fc717c575aba8a26cd0b2b113ce29781a2e298d484f6e420a87416aec";

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
