// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bufio"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"time"
)

const kMountPoint = "/mnt"

// Block device types output by blkid.
const (
	blkTypeLVM2Member = "LVM2_member"
	blkTypeEXT4       = "ext4"
)

var (
	reVgName       = regexp.MustCompile(`VG Name`)
	reDeviceMapper = regexp.MustCompile(`device-mapper`)
	reBlockDevice  = regexp.MustCompile(`Block device`)
	vgName         string
)

// getPartitionInfo finds a partition by its GPT label and returns its path and type.
func getPartitionInfo(label string) (string, string, error) {
	// Use blkid -t PARTLABEL="<label>" to get the specific line for the partition.
	cmd := exec.Command("blkid", "-t", fmt.Sprintf("PARTLABEL=%s", label))
	out, err := cmd.Output()
	if err != nil {
		// Note that blkid with exit with 2 if the device cannot be found.
		return "", "", fmt.Errorf("blkid command failed: %w", err)
	}

	line := strings.TrimSpace(string(out))

	// Parse the single line of output. Expected format:
	// /dev/mmcblk1p1: LABEL="H-STATE" ... TYPE="ext4" PARTLABEL="STATE" ...
	pathParts := strings.SplitN(line, ":", 2)
	if len(pathParts) < 1 {
		return "", "", fmt.Errorf("unexpected blkid output format (missing path): %s", line)
	}
	path := pathParts[0]

	blkType := ""
	if typeIdx := strings.Index(line, "TYPE=\""); typeIdx != -1 {
		lineAfterType := line[typeIdx+len("TYPE=\""):]
		if endTypeIdx := strings.Index(lineAfterType, "\""); endTypeIdx != -1 {
			blkType = lineAfterType[:endTypeIdx]
		}
	}

	return path, blkType, nil
}

func findString(s *bufio.Scanner, re *regexp.Regexp) (string, error) {
	for s.Scan() {
		if line := s.Text(); re.MatchString(line) {
			return line, nil
		}
	}
	return "", fmt.Errorf("not found")
}

func getVgName() (string, error) {
	// Wait for LVM devices to settle.
	exec.Command("lvm", "vgscan", "--mknodes").Run()
	c := exec.Command("lvm", "pvdisplay")
	out, err := c.StdoutPipe()
	if err != nil {
		return "", err
	}
	s := bufio.NewScanner(out)
	if err := c.Start(); err != nil {
		return "", err
	}
	ret, err := findString(s, reVgName)
	if err != nil {
		c.Wait()
		return "", err
	}
	c.Wait()
	return strings.Fields(ret)[2], nil
}

func makeTmetaNode() error {
	f, err := os.Open("/proc/devices")
	if err != nil {
		return err
	}
	defer f.Close()

	s := bufio.NewScanner(f)
	s.Split(bufio.ScanLines)

	ret, err := findString(s, reDeviceMapper)
	if err != nil {
		return err
	}
	major := strings.Fields(ret)[0]

	p := filepath.Join("/dev", "mapper", fmt.Sprintf("%s-thinpool_tmeta", vgName))
	return exec.Command("mknod", p, "b", major, "0").Run()
}

func makeKdumpNode() error {
	created := false
	kdumpPartition := filepath.Join("/dev", vgName, "kdump")

	if err := exec.Command("lvm", "lvdisplay", kdumpPartition).Run(); err != nil {
		t := fmt.Sprintf("%s/thinpool", vgName)
		if err := exec.Command("lvm", "lvcreate", "-V", "10G", "-n", "kdump", "-ay", "--thinpool", t).Run(); err != nil {
			return err
		}

		created = true
	} else {
		if err := exec.Command("lvm", "lvchange", "-ay", kdumpPartition).Run(); err != nil {
			return err
		}
	}

	c := exec.Command("lvm", "lvdisplay", kdumpPartition)
	out, err := c.StdoutPipe()
	if err != nil {
		return err
	}

	s := bufio.NewScanner(out)

	if err = c.Start(); err != nil {
		return err
	}

	ret, err := findString(s, reBlockDevice)
	if err != nil {
		c.Wait()
		return err
	}
	devs := strings.Split(strings.Fields(ret)[2], ":")
	major, minor := devs[0], devs[1]

	c.Wait()

	if err := exec.Command("mknod", "/dev/kdump", "b", major, minor).Run(); err != nil {
		return err
	}

	if !created {
		return nil
	}

	c = exec.Command("mke2fs", "-t", "ext4", "/dev/kdump")
	c.Stdout, c.Stderr = os.Stdout, os.Stderr
	return c.Run()
}

func mountKdumpPartitionWithLVM() error {
	var err error

	/*
	 * The program runs in ramfs and the system is going to reboot again anyway.
	 * Ignore all cleanup if any errors.
	 */

	if err = makeTmetaNode(); err != nil {
		return fmt.Errorf("Failed to makeTmetaNode(): %w", err)
	}

	if err = makeKdumpNode(); err != nil {
		return fmt.Errorf("Failed to makeKdumpNode(): %w", err)
	}

	if err = os.MkdirAll(kMountPoint, 0755); err != nil {
		return fmt.Errorf("Failed to MkdirAll(): %w", err)
	}

	if err = exec.Command("mount", "-t", "ext4", "/dev/kdump", kMountPoint).Run(); err != nil {
		return fmt.Errorf("Failed to mount: %w", err)
	}

	return nil
}

func mountStatefulPartition(path string) error {
	if _, err := os.Stat(path); err != nil {
		return fmt.Errorf("stateful partition device not found at %s: %w", path, err)
	}

	if err := os.MkdirAll(kMountPoint, 0755); err != nil {
		return fmt.Errorf("failed to MkdirAll(%s): %w", kMountPoint, err)
	}

	fmt.Printf("Mounting stateful partition %s at %s\n", path, kMountPoint)
	if err := exec.Command("mount", "-t", "ext4", path, kMountPoint).Run(); err != nil {
		return fmt.Errorf("failed to mount stateful partition: %w", err)
	}

	return nil
}

func runMakedumpfile(dumpPath string) error {
	name := time.Now().UTC().Format("20060102_150405")
	if err := os.MkdirAll(dumpPath, 0755); err != nil {
		return fmt.Errorf("failed to MkdirAll(%s): %w", dumpPath, err)
	}

	path := filepath.Join(dumpPath, fmt.Sprintf("%s.dmesg", name))
	c := exec.Command("makedumpfile", "--dump-dmesg", "/proc/vmcore", path)
	c.Stdout, c.Stderr = os.Stdout, os.Stderr
	if err := c.Run(); err != nil {
		return err
	}

	path = filepath.Join(dumpPath, fmt.Sprintf("%s.core", name))
	c = exec.Command("makedumpfile", "-c", "-d", "31", "/proc/vmcore", path)
	c.Stdout, c.Stderr = os.Stdout, os.Stderr
	return c.Run()
}

// saveCrashDump runs makedumpfile to save the crash dump into the stateful
// partition of the disk.
//
// Note that newer boards use LVM to manage the stateful partition while on the
// other boards the stateful partition is of type ext4.
//   - For the former case (LVM), we use LVM to create a logical volume for
//     kdump;
//   - For the latter case (ext4), we mount the ext4 partition directly and use
//     a folder under dev_image (which only exists when dev mode is enabled and
//     will be mounted to /usr/local in a normal boot) for kdump.
func saveCrashDump() error {
	statefulPath, blkType, err := getPartitionInfo("STATE")
	if err != nil {
		return fmt.Errorf("Failed to find STATE partition: %v\n", err)
	}

	fmt.Printf("Found STATE partition at %s with type %s\n", statefulPath, blkType)

	var dumpPath string
	switch blkType {
	case blkTypeLVM2Member:
		var err error
		vgName, err = getVgName()
		if err != nil {
			return fmt.Errorf("failed to get LVM VG name: %w", err)
		}
		if err := mountKdumpPartitionWithLVM(); err != nil {
			return err
		}
		dumpPath = kMountPoint

	case blkTypeEXT4:
		if err := mountStatefulPartition(statefulPath); err != nil {
			return err
		}
		dumpPath = filepath.Join(kMountPoint, "dev_image", "kdump")

	default:
		return fmt.Errorf("unsupported filesystem type for STATE partition: %s", blkType)
	}

	defer func() {
		exec.Command("sync").Run()
		exec.Command("umount", kMountPoint).Run()
	}()

	return runMakedumpfile(dumpPath)
}

func main() {
	/*
	 * u-root fork/exec "-defaultsh" twice.  A workaround to make sure it
	 * only runs once.
	 */
	if _, err := os.Stat("/tmp/kdump_dev_init"); err == nil {
		os.Exit(0)
	}
	f, _ := os.Create("/tmp/kdump_dev_init")
	f.Close()

	fmt.Println("=== Running kdump_dev_init ===")

	if err := saveCrashDump(); err != nil {
		fmt.Println(err)
		os.Exit(1)
	}
}
