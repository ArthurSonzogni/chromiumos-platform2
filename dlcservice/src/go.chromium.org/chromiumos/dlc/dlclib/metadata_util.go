// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dlclib

import (
	"os/exec"
)

// MetadataUtil is the exposed definition for DLC metadata util.
var MetadataUtil metadataUtil

// Internal types for namespacing.
type metadataUtil struct{}

// Read returns the `--get` of `id` from DLC metadata util.
func (mu metadataUtil) Read(id *string) ([]byte, error) {
	cmd := &exec.Cmd{
		Path: MetadataUtilPath,
		Args: append([]string{MetadataUtilPath}, "--get", "--id="+*id),
	}
	return cmd.Output()
}