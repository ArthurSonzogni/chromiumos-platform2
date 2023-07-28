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

var (
	reVgName       = regexp.MustCompile(`VG Name`)
	reDeviceMapper = regexp.MustCompile(`device-mapper`)
	reBlockDevice  = regexp.MustCompile(`Block device`)
	vgName         string
)

func findString(s *bufio.Scanner, re *regexp.Regexp) (string, error) {
	for s.Scan() {
		if line := s.Text(); re.MatchString(line) {
			return line, nil
		}
	}

	return "", fmt.Errorf("Not found")
}

func getVgName() (string, error) {
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

func setupVars() error {
	var err error

	/*
	 * It is observed that the DM/LVM may not be ready when the early
	 * userspace is running.  Retry few times for waiting.  We should
	 * revisit this if CONFIG_SMP is disabled in second kernel.
	 */
	tries, t := 0, time.NewTicker(time.Second)
	defer t.Stop()
	for range t.C {
		vgName, err = getVgName()
		if err == nil {
			break
		}

		fmt.Print(".")

		tries++
		if tries >= 10 {
			break
		}
	}
	if err != nil {
		return fmt.Errorf("Failed to getVgName(): %w\n", err)
	}
	fmt.Printf("vgName = %s\n", vgName)

	return err
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

	c = exec.Command("mkfs.ext4", "/dev/kdump")
	c.Stdout, c.Stderr = os.Stdout, os.Stderr
	return c.Run()
}

func mountKdumpDir() error {
	var err error

	/*
	 * The program runs in ramfs and the system is going to reboot again anyway.
	 * Ignore all cleanup if any errors.
	 */

	if err = makeTmetaNode(); err != nil {
		return fmt.Errorf("Failed to makeTmetaNode(): %w\n", err)
	}

	if err = makeKdumpNode(); err != nil {
		return fmt.Errorf("Failed to makeKdumpNode(): %w\n", err)
	}

	if err = os.MkdirAll(kMountPoint, 0755); err != nil {
		return fmt.Errorf("Failed to MkdirAll(): %w\n", err)
	}

	if err = exec.Command("mount", "-t", "ext4", "/dev/kdump", kMountPoint).Run(); err != nil {
		return fmt.Errorf("Failed to mount: %w\n", err)
	}

	return nil
}

func umountKdumpDir() {
	exec.Command("sync").Run()
	exec.Command("umount", kMountPoint).Run()
}

func generateCrashDump() error {
	var err error

	name := time.Now().UTC().Format("20060102_150405")

	if err = mountKdumpDir(); err != nil {
		return err
	}
	defer umountKdumpDir()

	path := filepath.Join(kMountPoint, fmt.Sprintf("%s.dmesg", name))
	c := exec.Command("makedumpfile", "--dump-dmesg", "/proc/vmcore", path)
	c.Stdout, c.Stderr = os.Stdout, os.Stderr
	if err = c.Run(); err != nil {
		return err
	}

	path = filepath.Join(kMountPoint, fmt.Sprintf("%s.core", name))
	c = exec.Command("makedumpfile", "-c", "-d", "31", "/proc/vmcore", path)
	c.Stdout, c.Stderr = os.Stdout, os.Stderr
	if err = c.Run(); err != nil {
		return err
	}

	return nil
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

	if err := setupVars(); err != nil {
		fmt.Println(err)
		os.Exit(1)
	}

	if err := generateCrashDump(); err != nil {
		fmt.Println(err)
	}
}
