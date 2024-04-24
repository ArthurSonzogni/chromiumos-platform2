// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dlclib

import (
	"os/exec"
)

// Util is the exposed definition for dlcservice utility.
var Util util

// Internal types for namespacing.
type util struct{}

// Read returns the `--dlc_state` of `id` from dlcservice utility.
func (u util) Read(id *string) ([]byte, error) {
	cmd := &exec.Cmd{
		Path: UtilPath,
		Args: append([]string{UtilPath}, "--dlc_state", "--id="+*id),
	}
	return cmd.Output()
}

// Uninstall will `--uninstall`/`--purge` the `id` with dlcservice utility.
func (u util) Uninstall(id *string) error {
	cmd := &exec.Cmd{
		Path: UtilPath,
		Args: append([]string{UtilPath}, "--uninstall", "--id="+*id),
	}
	return cmd.Run()
}

// Deploy will `--deploy` the `id` with dlcservice utility.
func (u util) Deploy(id *string) error {
	cmd := &exec.Cmd{
		Path: UtilPath,
		Args: []string{UtilPath, "--deploy", "--id=" + *id},
	}
	return cmd.Run()
}
