// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package introspect provides data type of introspection and its utility.
// Method and signal handlers are generated from introspection.
package introspect

// TODO(chromium:983008): Add checks for the presence of unexpected elements in XML files.

// Annotation adds settings to MethodArg, SignalArg and Method.
type Annotation struct {
	Name  string `xml:"name,attr"`
	Value string `xml:"value,attr"`
}

// MethodArg represents method argument or return value.
type MethodArg struct {
	Name       string     `xml:"name,attr"`
	Type       string     `xml:"type,attr"`
	Direction  string     `xml:"direction,attr"`
	Annotation Annotation `xml:"annotation"`
}

// Method represents method provided by a object through a interface.
type Method struct {
	Name        string       `xml:"name,attr"`
	Args        []MethodArg  `xml:"arg"`
	Annotations []Annotation `xml:"annotation"`
	DocString   string       `xml:"http://telepathy.freedesktop.org/wiki/DbusSpec#extensions-v0 docstring"`
}

// SignalArg represents signal message.
type SignalArg struct {
	Name       string     `xml:"name,attr"`
	Type       string     `xml:"type,attr"`
	Annotation Annotation `xml:"annotation"`
}

// Signal represents signal provided by a object through a interface.
type Signal struct {
	Name      string      `xml:"name,attr"`
	Args      []SignalArg `xml:"arg"`
	DocString string      `xml:"http://telepathy.freedesktop.org/wiki/DbusSpec#extensions-v0 docstring"`
}

// Property represents property provided by a object through a interface.
type Property struct {
	Name      string `xml:"name,attr"`
	Type      string `xml:"type,attr"`
	Access    string `xml:"access,attr"`
	DocString string `xml:"http://telepathy.freedesktop.org/wiki/DbusSpec#extensions-v0 docstring"`
}

// Interface represents interface provided by a object.
type Interface struct {
	Name       string     `xml:"name,attr"`
	Methods    []Method   `xml:"method"`
	Signals    []Signal   `xml:"signal"`
	Properties []Property `xml:"property"`
	DocString  string     `xml:"http://telepathy.freedesktop.org/wiki/DbusSpec#extensions-v0 docstring"`
}

// Introspection represents object specification required for generating
// method and signal handlers.
type Introspection struct {
	Name       string      `xml:"name,attr"`
	Interfaces []Interface `xml:"interface"`
}
