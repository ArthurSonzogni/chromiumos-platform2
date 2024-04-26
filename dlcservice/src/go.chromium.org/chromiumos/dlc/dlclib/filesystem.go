// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dlclib

import (
	"fmt"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"sync"
	"syscall"
)

// Filesystem is the exposed definition for filesystem helpers.
var Filesystem filesystem

// Internal types for namespacing.
type filesystem struct{}

const (
	mksquashfsPath = "/usr/bin/mksquashfs"
	unsquashfsPath = "/usr/bin/unsquashfs"

	ext2Path = "/sbin/mkfs.ext2"
	ext4Path = "/sbin/mkfs.ext4"

	e2fsckPath    = "/sbin/e2fsck"
	resize2fsPath = "/sbin/resize2fs"

	minExtBytes = 512 * 1024
)

// CreateSquashfs will create a squashfs.
func (fs filesystem) CreateSquashfs(inPath, outPath string) error {
	args := []string{"-4k-align", "-noappend"}
	return createSquashfs(inPath, outPath, args...)
}

// CreateSquashfsNoCompression will create a squashfs with no compression.
func (fs filesystem) CreateSquashfsNoCompression(inPath, outPath string) error {
	args := []string{"-4k-align", "-noappend", "-noI", "-noD", "-noF", "-noX", "-no-duplicates"}
	return createSquashfs(inPath, outPath, args...)
}

func createSquashfs(inPath, outPath string, args ...string) error {
	cmd := &exec.Cmd{
		Path: mksquashfsPath,
		Args: append([]string{mksquashfsPath, inPath, outPath}, args...),
	}
	return cmd.Run()
}

// Extract will extract the given squashfs image.
func (fs filesystem) ExtractSquashfs(inPath, outPath string) error {
	cmd := &exec.Cmd{
		Path: unsquashfsPath,
		Args: []string{unsquashfsPath, "-d", outPath, inPath},
	}
	return cmd.Run()
}

// CreateExt2 will create an ext2 filesystem image.
func (fs filesystem) CreateExt2(inPath, outPath string) error {
	return createExt(2, inPath, outPath)
}

// CreateExt4 will create an ext2 filesystem image.
func (fs filesystem) CreateExt4(inPath, outPath string) error {
	return createExt(4, inPath, outPath)
}

func createExt(extNum int, inPath, outPath string) error {
	var err error
	var f *os.File
	if f, err = os.Create(outPath); err != nil {
		return fmt.Errorf("dlclib.createExt: failed to create output path: %w", err)
	}
	defer f.Close()

	var bytes int64
	if bytes, err = DirectorySize(inPath); err != nil {
		return fmt.Errorf("dlclib.createExt: failed to get directory size: %w", err)
	}
	if bytes < minExtBytes {
		bytes = minExtBytes
	}

	if err = f.Truncate(bytes); err != nil {
		return fmt.Errorf("dlclib.createExt: failed to truncate: %w", err)
	}

	switch extNum {
	case 2:
		var output []byte
		if output, err = exec.Command(ext2Path, "-b", "4096", outPath).CombinedOutput(); err != nil {
			return fmt.Errorf("dlclib.createExt: failed to create ext2: %s, %w", output, err)
		}
	case 4:
		var output []byte
		if output, err = exec.Command(ext4Path, "-b", "4096", "-O", "^has_journal", outPath).CombinedOutput(); err != nil {
			return fmt.Errorf("dlclib.createExt: failed to create ext4: %s, %w", output, err)
		}
	default:
		return fmt.Errorf("dlclib.createExt: unsupported ext number: %v", extNum)
	}

	var tmpDir string
	if tmpDir, err = os.MkdirTemp("", "dlclib_*"); err != nil {
		return fmt.Errorf("dlclib.createExt: failed to mkdir temp: %w", err)
	}
	defer os.RemoveAll(tmpDir)

	var loopDev string
	if loopDev, err = loopDevAttach(outPath); err != nil {
		return fmt.Errorf("dlclib.createExt: failed to attach to loop device: %w", err)
	}
	var onceLoopDev sync.Once
	detachFunc := func() {
		onceLoopDev.Do(func() {
			loopDevDetach(loopDev)
		})
	}
	defer detachFunc()

	if err = syscall.Mount(loopDev, tmpDir, "ext"+strconv.Itoa(extNum), syscall.MS_NODEV|syscall.MS_NOEXEC|syscall.MS_NOSUID, ""); err != nil {
		return fmt.Errorf("dlclib.createExt: failed to mount: %w", err)
	}
	var once sync.Once
	unmountFunc := func() {
		once.Do(func() {
			syscall.Unmount(tmpDir, syscall.MNT_FORCE)
		})
	}
	defer unmountFunc()

	if err = CopyDirectory(inPath, tmpDir); err != nil {
		return fmt.Errorf("dlclib.createExt: failed to copy directory, %w", err)
	}

	unmountFunc()
	detachFunc()

	if err = exec.Command(e2fsckPath, "-y", "-f", outPath).Run(); err != nil {
		return fmt.Errorf("dlclib.createExt: failed to e2fsck: %w", err)
	}

	if err = exec.Command(resize2fsPath, "-M", outPath).Run(); err != nil {
		return fmt.Errorf("dlclib.createExt: failed to resize2fs: %w", err)
	}

	return nil
}

func loopDevAttach(p string) (string, error) {
	var err error
	var dev []byte

	if dev, err = exec.Command("sudo", "losetup", "-f", "--show", p).Output(); err != nil {
		return "", err
	}

	return strings.TrimSpace(string(dev)), nil
}

func loopDevDetach(p string) error {
	return exec.Command("losetup", "-d", p).Run()
}

// ExtractExt2 will extract the given ext2 filesystem image.
func (fs filesystem) ExtractExt2(inPath, outPath string) error {
	return extractExt(2, inPath, outPath)
}

// ExtractExt4 will extract the given ext2 filesystem image.
func (fs filesystem) ExtractExt4(inPath, outPath string) error {
	return extractExt(4, inPath, outPath)
}

func extractExt(extNum int, inPath, outPath string) error {
	var err error

	var tmpDir string
	if tmpDir, err = os.MkdirTemp("", "dlclib_*"); err != nil {
		return fmt.Errorf("dlclib.extractExt: failed to mkdir temp: %w", err)
	}
	defer os.RemoveAll(tmpDir)

	var loopDev string
	if loopDev, err = loopDevAttach(inPath); err != nil {
		return fmt.Errorf("dlclib.extractExt: failed to attach to loop device: %w", err)
	}
	defer loopDevDetach(loopDev)

	if err = syscall.Mount(loopDev, tmpDir, "ext"+strconv.Itoa(extNum), syscall.MS_NODEV|syscall.MS_NOEXEC|syscall.MS_NOSUID, ""); err != nil {
		return fmt.Errorf("dlclib.extractExt: failed to mount: %w", err)
	}
	defer syscall.Unmount(tmpDir, syscall.MNT_FORCE)

	if err = os.Mkdir(outPath, 0755); err != nil {
		return fmt.Errorf("dlclib.extractExt: failed to mkdir %s: %w", outPath, err)
	}

	if err = CopyDirectory(tmpDir, outPath); err != nil {
		return fmt.Errorf("dlclib.extractExt: failed to copy directory: %w", err)
	}

	return nil
}
