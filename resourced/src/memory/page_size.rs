// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use once_cell::sync::Lazy;

static PAGE_SIZE: Lazy<usize> = Lazy::new(|| {
    // SAFETY: sysconf is memory safe.
    unsafe { libc::sysconf(libc::_SC_PAGE_SIZE) as usize }
});

pub fn get_page_size() -> usize {
    *PAGE_SIZE
}
