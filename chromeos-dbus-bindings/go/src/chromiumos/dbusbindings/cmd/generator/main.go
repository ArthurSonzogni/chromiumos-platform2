// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package main implements the generate-chromeos-dbus-bindings used to generate
// dbus bindings
package main

import (
	"flag"
	"io/ioutil"
	"log"
	"os"

	"chromiumos/dbusbindings/generate/adaptor"
	"chromiumos/dbusbindings/generate/methodnames"
	"chromiumos/dbusbindings/generate/proxy"
	"chromiumos/dbusbindings/introspect"
	"chromiumos/dbusbindings/serviceconfig"
)

func main() {
	serviceConfigPath := flag.String("service-config", "", "the DBus service configuration file for the generator.")
	methodNamesPath := flag.String("method-names", "", "the output header file with string constants for each method name")
	adaptorPath := flag.String("adaptor", "", "the output header file name containing the DBus adaptor class")
	proxyPath := flag.String("proxy", "", "the output header file name containing the DBus proxy class")
	flag.Parse()

	var sc serviceconfig.Config
	if *serviceConfigPath != "" {
		c, err := serviceconfig.Load(*serviceConfigPath)
		if err != nil {
			log.Fatalf("Failed to read config file %s: %v", *serviceConfigPath, err)
		}
		sc = *c
	}

	var introspections []introspect.Introspection
	for _, path := range flag.Args() {
		b, err := ioutil.ReadFile(path)
		if err != nil {
			log.Fatalf("Failed to read file %s: %v\n", path, err)
		}

		introspection, err := introspect.Parse(b)
		if err != nil {
			log.Fatalf("Failed to parse interface file %s: %v\n", path, err)
		}

		introspections = append(introspections, introspection)
	}

	if *methodNamesPath != "" {
		f, err := os.Create(*methodNamesPath)
		if err != nil {
			log.Fatalf("Failed to create file %s: %v\n", *methodNamesPath, err)
		}
		defer func() {
			if err := f.Close(); err != nil {
				log.Fatalf("Failed to close file %s: %v\n", *methodNamesPath, err)
			}
		}()

		if err := methodnames.Generate(introspections, f); err != nil {
			log.Fatalf("Failed to generate methodnames: %v\n", err)
		}
	}

	if *adaptorPath != "" {
		f, err := os.Create(*adaptorPath)
		if err != nil {
			log.Fatalf("Failed to create adaptor file %s: %v\n", *adaptorPath, err)
		}
		defer func() {
			if err := f.Close(); err != nil {
				log.Fatalf("Failed to close file %s: %v\n", *adaptorPath, err)
			}
		}()

		if err := adaptor.Generate(introspections, f, *adaptorPath); err != nil {
			log.Fatalf("Failed to generate adaptor: %v\n", err)
		}
	}

	if *proxyPath != "" {
		f, err := os.Create(*proxyPath)
		if err != nil {
			log.Fatalf("Failed to create proxy file %s: %v\n", *proxyPath, err)
		}
		defer func() {
			if err := f.Close(); err != nil {
				log.Fatalf("Failed to close file %s: %v\n", *proxyPath, err)
			}
		}()

		if err := proxy.Generate(introspections, f, *proxyPath, sc); err != nil {
			log.Fatalf("Failed to generate proxy: %v\n", err)
		}
	}
}
