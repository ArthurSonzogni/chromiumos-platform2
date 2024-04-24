// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dlclib

const (
	// ImageFile is the on-disk DLC image name.
	ImageFile = "dlc.img"

	// Package is the default DLC package.
	Package = "package"

	// Root is the default DLC root directory within images.
	Root = "root"

	// PreloadPath is the DLC path for preloaded images.
	PreloadPath = "/var/cache/dlc-images"

	// DeployPath is the DLC path for deployed images.
	DeployPath = "/mnt/stateful_partition/unencrypted/dlc-deployed-images"

	// MetadataUtilPath is the DLC metadata utility.
	MetadataUtilPath = "/usr/bin/dlc_metadata_util"

	// UtilPath is the core DLC utility.
	UtilPath = "/usr/bin/dlcservice_util"

	// ImageloaderRootMountPath is the tmpfs root mount managed by imageloader service.
	ImageloaderRootMountPath = "/run/imageloader"

	// DlcserviceUID is the dlcservice user ID.
	DlcserviceUID = 20118

	// DlcserviceGID is the dlcservice group ID.
	DlcserviceGID = 20118
)
