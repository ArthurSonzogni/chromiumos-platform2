// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The dlctool executable allows for modification of DLCs directly on the device.
package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"os"
	"os/exec"
	"path"
)

const (
	dlcPreloadPath             = "/var/cache/dlc-images"
	dlcserviceMetadataUtilPath = "/usr/bin/dlc_metadata_util"
	dlcserviceUtilPath         = "/usr/bin/dlcservice_util"
	dlctoolShellPath           = "/usr/local/bin/dlctool-shell"
	dlcImageFile               = "dlc.img"

	unsquashfsPath = "/usr/bin/unsquashfs"
)

var (
	flagID     *string = flag.String("id", "", "ID of the DLC to [un]pack.")
	flagUnpack *bool   = flag.Bool("unpack", false, "To unpack the DLC passed.")
	flagShell  *bool   = flag.Bool("shell", true, "Force shell usage.")
)

// Defined, but ignored.
var (
	flagCompress   *bool = flag.Bool("compress", true, "Compress the image. Slower to pack but creates smaller images.")
	flagNoCompress *bool = flag.Bool("nocompress", false, "Don't compress the image.")
)

func initFlags() {
	// Parse only after flag definitions.
	flag.Parse()

	// Special treatment for select flags.
	flag.Visit(func(f *flag.Flag) {
		if f.Name == "shell" && f.Value.(flag.Getter).Get().(bool) == true {
			log.Fatal("Please don't explicitly pass in `shell` flag with `true`.")
		}
	})
	if *flagID == "" {
		log.Fatal("Cannot pass empty ID.")
	}
}

func dlctoolShell(args []string) {
	cmd := &exec.Cmd{
		Path:   dlctoolShellPath,
		Args:   append([]string{dlctoolShellPath}, args...),
		Stdout: os.Stdout,
		Stderr: os.Stderr,
		Env:    os.Environ(),
	}
	if err := cmd.Run(); err != nil {
		log.Fatalf("%v failed: %v", os.Args[0], err)
	}
}

func readState(id *string) ([]byte, error) {
	cmd := &exec.Cmd{
		Path: dlcserviceUtilPath,
		Args: append([]string{dlcserviceUtilPath}, "--dlc_state", "--id="+*id),
	}
	return cmd.Output()
}

func readMetadata(id *string) ([]byte, error) {
	cmd := &exec.Cmd{
		Path: dlcserviceMetadataUtilPath,
		Args: append([]string{dlcserviceMetadataUtilPath}, "--get", "--id="+*id),
	}
	return cmd.Output()
}

func isDlcInstalled(id *string) bool {
	out, err := readState(id)
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
	_, err := os.Stat(path.Join(dlcPreloadPath, *id, "package", dlcImageFile))
	return !os.IsNotExist(err)
}

func isDlcScaled(id *string) bool {
	out, err := readMetadata(id)
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
	out, err := readMetadata(id)
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
	out, err := readState(id)
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

func installDlc(id *string) error {
	cmd := &exec.Cmd{
		Path: dlcserviceUtilPath,
		Args: append([]string{dlcserviceUtilPath}, "--install", "--id="+*id),
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

func extractDlc(id, path *string) error {
	// TODO(b/335722339): Add support for other filesystems based on image type.
	cmd := &exec.Cmd{
		Path: unsquashfsPath,
		Args: []string{unsquashfsPath, "-d", *path, getDlcImagePath(id)},
	}

	if err := cmd.Run(); err != nil {
		return fmt.Errorf("extractDlc: failed to decompress: %w", err)
	}

	return nil
}

func unpackDlc(id, path *string) error {
	if _, err := os.Stat(*path); !os.IsNotExist(err) {
		return fmt.Errorf("%s is a path which already exists", *path)
	}

	if err := tryInstallingDlc(id); err != nil {
		return fmt.Errorf("unpackDlc: failed installing DLC: %w", err)
	}

	if err := extractDlc(id, path); err != nil {
		return fmt.Errorf("unpackDlc: failed extracting: %w", err)
	}

	return nil
}

func main() {
	initFlags()

	args := flag.Args()
	if len(args) != 1 {
		log.Fatal("Please only pass in <path>.")
	}
	path := path.Clean(args[0])

	if *flagUnpack {
		log.Printf("Unpacking DLC (%s) to: %s\n", *flagID, path)
		if err := unpackDlc(flagID, &path); err != nil {
			log.Fatalf("Unpacking DLC (%s) failed: %v", *flagID, err)
		}
		return
	}

	if *flagShell {
		dlctoolShell(os.Args[1:])
		return
	}

	log.Fatal("Please use shell variant.")
}
