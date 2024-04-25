// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dlclib

const (
	// ImageFile is the on-disk DLC image name.
	ImageFile = "dlc.img"

	// PreloadPath is the DLC path for preloaded images.
	PreloadPath = "/var/cache/dlc-images"

	// MetadataUtilPath is the DLC metadata utility.
	MetadataUtilPath = "/usr/bin/dlc_metadata_util"

	// UtilPath is the core DLC utility.
	UtilPath = "/usr/bin/dlcservice_util"

	// ToolShellPath is the `dlctool` shell path. (To be deprecated per deshell'ing)
	ToolShellPath = "/usr/local/bin/dlctool-shell"
)
