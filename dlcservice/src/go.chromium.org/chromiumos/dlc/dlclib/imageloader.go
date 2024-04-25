// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dlclib

import (
	"os/exec"
	"path"
)

// Imageloader is the exposed definition for imageloader.
var Imageloader imageloader

// Internal types for namespacing.
type imageloader struct{}

func (i imageloader) Unmount(id *string) error {
	cmd := &exec.Cmd{
		Path: UtilPath,
		Args: []string{UtilPath, "--unmount", "--mount_point=" + path.Join(ImageloaderRootMountPath, *id, Package)},
	}
	return cmd.Run()
}
