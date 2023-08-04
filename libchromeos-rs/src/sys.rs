// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Re-export types from crosvm_base that are used in CrOS.
//
// WARNING: using crosvm_base from CrOS is deprecated, do not add to this list.

// Still used by libcras
pub use crosvm_base::unix::SharedMemory;
pub use crosvm_base::Error;
