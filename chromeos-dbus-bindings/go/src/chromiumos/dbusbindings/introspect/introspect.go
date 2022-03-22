// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package introspect provides data type of introspection and its utility.
// Method and signal handlers are generated from introspection.
package introspect

// TODO(chromium:983008): Add checks for the presence of unexpected elements in XML files.

// MethodKind is an enum to represent the kind of a method.
type MethodKind int

const (
	// MethodKindSimple indicates that the method doesn't fail and no brillo::ErrorPtr argument is given.
	MethodKindSimple MethodKind = iota

	// MethodKindNormal indicates that the method returns false and sets a brillo::ErrorPtr on failure.
	MethodKindNormal

	// MethodKindAsync indicates that instead of returning "out" arguments directly,
	// the method takes a DBusMethodResponse argument templated on the types of the "out" arguments.
	MethodKindAsync

	// MethodKindRaw indicates that the method takes a dbus::MethodCall and dbus::ExportedObject::ResponseSender
	// object directly.
	MethodKindRaw
)

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

// InputArguments returns the array of input arguments extracted from method arguments.
func (m *Method) InputArguments() []MethodArg {
	var ret []MethodArg
	for _, a := range m.Args {
		if a.Direction == "in" || a.Direction == "" { // default direction is "in"
			ret = append(ret, a)
		}
	}
	return ret
}

// OutputArguments returns the array of output arguments extracted from method arguments.
func (m *Method) OutputArguments() []MethodArg {
	var ret []MethodArg
	for _, a := range m.Args {
		if a.Direction == "out" {
			ret = append(ret, a)
		}
	}
	return ret
}

// Kind returns the kind of method.
func (m *Method) Kind() MethodKind {
	for _, a := range m.Annotations {
		// Support GLib.Async annotation as well.
		if a.Name == "org.freedesktop.DBus.GLib.Async" {
			return MethodKindAsync
		}

		if a.Name == "org.chromium.DBus.Method.Kind" {
			switch a.Value {
			case "simple":
				return MethodKindSimple
			case "normal":
				return MethodKindNormal
			case "async":
				return MethodKindAsync
			case "raw":
				return MethodKindRaw
			}
		}
	}

	return MethodKindSimple
}

// IncludeDBusMessage returns true if the method needs a message argument added.
func (m *Method) IncludeDBusMessage() bool {
	for _, a := range m.Annotations {
		if a.Name == "org.chromium.DBus.Method.IncludeDBusMessage" {
			return a.Value == "true"
		}
	}
	return false
}

// Const returns true if the method is a const member function.
func (m *Method) Const() bool {
	for _, a := range m.Annotations {
		if a.Name == "org.chromium.DBus.Method.Const" {
			return a.Value == "true"
		}
	}
	return false
}
