// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generates the Rust D-Bus bindings for update_engine.

use std::path::Path;

use chromeos_dbus_bindings::{self, generate_module, BindingsType};

const SOURCE_DIR: &str = ".";

// (<module name>, <relative path to source xml>)
const BINDINGS_TO_GENERATE: &[(&str, &str, BindingsType)] = &[(
    "org_chromium_updateengineinterface",
    "dbus_bindings/org.chromium.UpdateEngineInterface.dbus-xml",
    BindingsType::Client(None),
)];

fn main() {
    generate_module(Path::new(SOURCE_DIR), BINDINGS_TO_GENERATE).unwrap();
}
