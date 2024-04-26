// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The dlctool executable allows for modification of DLCs directly on the device.
package main

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"strconv"
	"strings"

	"go.chromium.org/chromiumos/dlc/dlclib"
	"go.chromium.org/chromiumos/dlc/dlctool/parse"
)

const (
	verityPath = "/usr/bin/verity"
)

func isDlcInstalled(id *string) bool {
	out, err := dlclib.Util.Read(id)
	if err != nil {
		log.Fatalf("Failed to read state: %v", err)
	}

	state := struct {
		State int `json:"state"`
	}{}
	err = json.Unmarshal(out, &state)

	if err != nil {
		log.Fatalf("Failed to unmarshal DLC (%s) state", *id)
	}

	return state.State == 2
}

func isDlcPreloadable(id *string) bool {
	_, err := os.Stat(path.Join(dlclib.PreloadPath, *id, "package", dlclib.ImageFile))
	return !os.IsNotExist(err)
}

func isDlcScaled(id *string) bool {
	out, err := dlclib.MetadataUtil.Read(id)
	if err != nil {
		log.Fatalf("Failed to read metadata: %v", err)
	}

	metadata := struct {
		Manifest struct {
			Scaled bool `json:"scaled"`
		} `json:"manifest"`
	}{}
	err = json.Unmarshal(out, &metadata)

	if err != nil {
		log.Fatalf("Failed to unmarshal DLC (%s)", *id)
	}

	return metadata.Manifest.Scaled
}

func isDlcForceOTA(id *string) bool {
	out, err := dlclib.MetadataUtil.Read(id)
	if err != nil {
		log.Fatalf("Failed to read metadata: %v", err)
	}

	metadata := struct {
		Manifest struct {
			ForceOTA bool `json:"force-ota"`
		} `json:"manifest"`
	}{}
	err = json.Unmarshal(out, &metadata)

	if err != nil {
		log.Fatalf("Failed to unmarshal DLC (%s)", *id)
	}

	return metadata.Manifest.ForceOTA
}

func getDlcImagePath(id *string) string {
	out, err := dlclib.Util.Read(id)
	if err != nil {
		log.Fatalf("dlcImagePath: Failed to read state: %v", err)
	}

	state := struct {
		ImagePath string `json:"image_path"`
	}{}
	err = json.Unmarshal(out, &state)

	if err != nil {
		log.Fatalf("dlcImagePath: Failed to unmarshal DLC (%s)", *id)
	}

	return state.ImagePath
}

func getDlcManifest(id *string) json.RawMessage {
	out, err := dlclib.MetadataUtil.Read(id)
	if err != nil {
		log.Fatalf("getDlcManifest: failed to read state: %v", err)
	}

	state := struct {
		Manifest json.RawMessage `json:"manifest"`
	}{}
	err = json.Unmarshal(out, &state)

	if err != nil {
		log.Fatalf("getDlcManifest: failed to unmarshal DLC (%s)", *id)
	}

	return state.Manifest
}

func installDlc(id *string) error {
	cmd := &exec.Cmd{
		Path: dlclib.UtilPath,
		Args: []string{dlclib.UtilPath, "--install", "--id=" + *id},
	}
	return cmd.Run()
}

func tryInstallingDlc(id *string) error {
	if isDlcInstalled(id) {
		log.Printf("DLC (%s) is already installed, continuing...\n", *id)
		return nil
	}

	if isDlcPreloadable(id) {
		log.Printf("Trying to install DLC (%s) because it's preloaded.\n", *id)
	} else if isDlcScaled(id) {
		log.Printf("Trying to install DLC (%s) because it's scaled.\n", *id)
	} else if isDlcForceOTA(id) {
		log.Printf("Trying to install DLC (%s) because it's force-ota.\n", *id)
	} else {
		return fmt.Errorf("tryInstallingDlc failed: Can't install DLC (%s)", *id)
	}

	if err := installDlc(id); err != nil {
		return fmt.Errorf("tryInstallingDlc failed: %w", err)
	}
	log.Printf("Installed DLC (%s)\n", *id)
	return nil
}

func unpackDlc(fsType, id, path *string) error {
	if _, err := os.Stat(*path); !os.IsNotExist(err) {
		return fmt.Errorf("%s is a path which already exists", *path)
	}

	if err := tryInstallingDlc(id); err != nil {
		return fmt.Errorf("unpackDlc: failed installing DLC: %w", err)
	}

	switch *fsType {
	case "squashfs":
		if err := dlclib.Filesystem.ExtractSquashfs(getDlcImagePath(id), *path); err != nil {
			return fmt.Errorf("unpackDlc: failed to extract squashfs: %w", err)
		}
	case "ext2":
		if err := dlclib.Filesystem.ExtractExt2(getDlcImagePath(id), *path); err != nil {
			return fmt.Errorf("unpackDlc: failed to extract ext2: %w", err)
		}
	case "ext4":
		if err := dlclib.Filesystem.ExtractExt4(getDlcImagePath(id), *path); err != nil {
			return fmt.Errorf("unpackDlc: failed to extract ext4: %w", err)
		}
	default:
		return fmt.Errorf("unpackDlc: unsupported fs-type: %v", *fsType)
	}

	return nil
}

func packDlc(fsType, id, p *string, compress bool) error {
	var err error

	if !dlclib.IsWritable("/") {
		return fmt.Errorf("packDlc: disable rootfs verification to use this script, Reference: https://www.chromium.org/chromium-os/developer-library/guides/device/developer-mode/#making-changes-to-the-filesystem")
	}

	_ = dlclib.Imageloader.Unmount(id)
	_ = dlclib.Util.Uninstall(id)

	var exists bool
	if exists, err = dlclib.PathExists(path.Join(*p, dlclib.Root)); err != nil {
		return fmt.Errorf("packDlc: failed to check for root: %v", err)
	} else if !exists {
		return fmt.Errorf("packDlc: root directory is missing")
	}

	var tmpDir string
	if tmpDir, err = os.MkdirTemp("", "dlc_*"); err != nil {
		return fmt.Errorf("packDlc: failed to create temporary directory: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	imgOutPath := path.Join(tmpDir, dlclib.ImageFile)
	switch *fsType {
	case "squashfs":
		if compress {
			if err = dlclib.Filesystem.CreateSquashfs(*p, imgOutPath); err != nil {
				return fmt.Errorf("packDlc: failed to create squashfs: %v", err)
			}
		} else if err = dlclib.Filesystem.CreateSquashfsNoCompression(*p, imgOutPath); err != nil {
			return fmt.Errorf("packDlc: failed to create squashfs no compression: %v", err)
		}
	case "ext2":
		if err = dlclib.Filesystem.CreateExt2(*p, imgOutPath); err != nil {
			return fmt.Errorf("packDlc: failed to create ext2: %v", err)
		}
	case "ext4":
		if err = dlclib.Filesystem.CreateExt4(*p, imgOutPath); err != nil {
			return fmt.Errorf("packDlc: failed to create ext4: %v", err)
		}
	default:
		return fmt.Errorf("packDlc: unsupported fs-type: %v", *fsType)
	}

	var imgBlockCount int64
	if imgBlockCount, err = dlclib.GetFileSizeInBlocks(imgOutPath); err != nil {
		return fmt.Errorf("packDlc: failed to get file size in blocks: %v", err)
	}

	// Handle images that are < 2 * 4KiB bytes, otherwise, dm-verity will choke.
	if imgBlockCount < 2 {
		imgBlockCount = 2
	}
	if err = os.Truncate(imgOutPath, imgBlockCount*4096); err != nil {
		return fmt.Errorf("packDlc: failed to truncate image: %v", err)
	}

	var table []byte
	hashtreeOutPath := path.Join(tmpDir, "hashtree")
	{
		cmd := &exec.Cmd{
			Path: verityPath,
			Args: []string{
				verityPath,
				"--mode=create",
				"--alg=sha256",
				"--payload=" + imgOutPath,
				"--payload_blocks=" + strconv.FormatInt(imgBlockCount, 10),
				"--hashtree=" + hashtreeOutPath,
				"--salt=random",
			},
		}

		if table, err = cmd.Output(); err != nil {
			return fmt.Errorf("packDlc: failed to run verity: %v", err)
		}
		table = []byte(strings.TrimSpace(string(table)))
	}

	tableOutPath := path.Join(tmpDir, "table")
	if err = ioutil.WriteFile(tableOutPath, table, 0644); err != nil {
		return fmt.Errorf("packDlc: failed to write table: %v", err)
	}

	if err = dlclib.AppendFile(hashtreeOutPath, imgOutPath); err != nil {
		return fmt.Errorf("packDlc: failed to append hashtree to image: %v", err)
	}

	manifest := getDlcManifest(id)
	manifestJSON := struct {
		All map[string]interface{} `json:"-"`
	}{}
	if err = json.Unmarshal(manifest, &manifestJSON.All); err != nil {
		return fmt.Errorf("packDlc: failed to unmarshal manifest json into `All`: %v", err)
	}

	{
		var hash string
		if hash, err = dlclib.Sha256Sum(imgOutPath); err != nil {
			return fmt.Errorf("packDlc: failed to get sha256sum for image: %v", err)
		}
		manifestJSON.All["image-sha256-hash"] = hash
	}
	{
		var hash string
		if hash, err = dlclib.Sha256Sum(tableOutPath); err != nil {
			return fmt.Errorf("packDlc: failed to get sha256sum for table: %v", err)
		}
		manifestJSON.All["table-sha256-hash"] = hash
	}
	{
		var imgBlockCountWithHashtree int64
		if imgBlockCountWithHashtree, err = dlclib.GetFileSizeInBlocks(imgOutPath); err != nil {
			return fmt.Errorf("packDlc: failed to get file size in blocks: %v", err)
		}
		newSize := strconv.FormatInt(imgBlockCountWithHashtree*4096, 10)
		manifestJSON.All["size"] = newSize
		manifestJSON.All["pre-allocated-size"] = newSize
		manifestJSON.All["fs-type"] = *fsType
	}

	metadata := struct {
		Manifest map[string]interface{} `json:"manifest"`
		Table    string                 `json:"table"`
	}{
		Manifest: manifestJSON.All,
		Table:    string(table),
	}
	var b []byte
	if b, err = json.Marshal(metadata); err != nil {
		return fmt.Errorf("packDlc: failed to marshal metadata: %v", err)
	}

	if err = dlclib.MetadataUtil.Write(id, b); err != nil {
		return fmt.Errorf("packDlc: failed to write metadata: %v", err)
	}

	if b, err = json.MarshalIndent(metadata, "", "    "); err != nil {
		return fmt.Errorf("packDlc: failed to marshal indent metadata: %v", err)
	} else {
		log.Printf("The DLC metadata is successfully updated to: %v", string(b))
	}

	deployIDRootPath := path.Join(dlclib.DeployPath, *id)
	deployOutPath := path.Join(deployIDRootPath, dlclib.Package)
	if err = os.MkdirAll(deployOutPath, 0755); err != nil {
		return fmt.Errorf("packDlc: failed to mkdir deploy path: %v", err)
	}

	if err = dlclib.CopyFile(imgOutPath, path.Join(deployOutPath, dlclib.ImageFile)); err != nil {
		return fmt.Errorf("packDlc: failed to move image to deploy path: %v", err)
	}

	if err = filepath.Walk(deployIDRootPath, func(subpath string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if err = os.Chmod(subpath, 0755); err != nil {
			return err
		}
		if err = os.Chown(subpath, dlclib.DlcserviceUID, dlclib.DlcserviceGID); err != nil {
			return err
		}
		return nil
	}); err != nil {
		return fmt.Errorf("packDlc: failed to chmod: %v", err)
	}

	if err = exec.Command("stop", "imageloader").Run(); err != nil {
		return fmt.Errorf("packDlc: failed to stop imageloader: %v", err)
	}

	if err = exec.Command("stop", "dlcservice").Run(); err != nil {
		return fmt.Errorf("packDlc: failed to stop dlcservice: %v", err)
	}

	if err = exec.Command("start", "dlcservice").Run(); err != nil {
		return fmt.Errorf("packDlc: failed to start dlcservice: %v", err)
	}

	if err = dlclib.Util.Deploy(id); err != nil {
		return fmt.Errorf("packDlc: failed to deploy: %v", err)
	}

	return nil
}

func main() {
	dlclib.Init()
	p, err := parse.Args(os.Args[0], os.Args[1:])
	if err != nil {
		log.Fatalf("Parsing flags failed: %v", err)
	}

	if *parse.FlagUnpack {
		log.Printf("Unpacking DLC (%s) to: %s\n", *parse.FlagID, p)
		if err := unpackDlc(parse.FlagFsType, parse.FlagID, &p); err != nil {
			log.Fatalf("Unpacking DLC (%s) failed: %v", *parse.FlagID, err)
		}
		return
	}

	log.Printf("Packing DLC (%s) from: %s\n", *parse.FlagID, p)
	if err := packDlc(parse.FlagFsType, parse.FlagID, &p, *parse.FlagCompress); err != nil {
		log.Fatalf("Packing DLC (%s) failed: %v", *parse.FlagID, err)
	}
}
