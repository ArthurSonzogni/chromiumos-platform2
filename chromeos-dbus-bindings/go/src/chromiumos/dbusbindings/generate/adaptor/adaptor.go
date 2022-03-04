// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package adaptor outputs a adaptor based on introspects.
package adaptor

import (
	"io"
	"text/template"

	"chromiumos/dbusbindings/generate/genutil"
	"chromiumos/dbusbindings/introspect"
)

type templateArgs struct {
	Introspects []introspect.Introspection
	HeaderGuard string
}

var funcMap = template.FuncMap{
	"makeInterfaceName": genutil.MakeInterfaceName,
	"makeAdaptorName":   genutil.MakeAdaptorName,
	"makeFullItfName":   genutil.MakeFullItfName,
	"extractNameSpaces": genutil.ExtractNameSpaces,
	"reverse":           genutil.Reverse,
}

const templateText = `// Automatic generation of D-Bus interfaces:
{{range .Introspects}}{{range .Interfaces -}}
//  - {{.Name}}
{{end}}{{end -}}
#ifndef {{.HeaderGuard}}
#define {{.HeaderGuard}}
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <base/files/scoped_file.h>
#include <dbus/object_path.h>
#include <brillo/any.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/dbus/exported_object_manager.h>
#include <brillo/dbus/file_descriptor.h>
#include <brillo/variant_dictionary.h>
{{range $introspect := .Introspects}}{{range .Interfaces -}}
{{$itfName := makeInterfaceName .Name -}}
{{$className := makeAdaptorName .Name -}}
{{$fullItfName := makeFullItfName .Name}}
{{range extractNameSpaces .Name -}}
namespace {{.}} {
{{end}}
// Interface definition for {{$fullItfName}}.
{{with .DocString -}}
{{.}}                {{- /* TODO(chromium:983008): Format the comment. */}}
{{end -}}
class {{$itfName}} {
 public:
  virtual ~{{$itfName}}() = default;
  {{- with .Methods}}
  {{- /* TODO(chromium:983008): Add interface methods */}}
  {{- end}}
};

// Interface adaptor for {{$fullItfName}}.
class {{$className}} {
 public:
  {{- if .Methods}}
  {{$className}}({{$itfName}}* interface) : interface_(interface) {}
  {{- else}}
  {{$className}}({{$itfName}}* /* interface */) {}
  {{- end}}
  {{$className}}(const {{$className}}&) = delete;
  {{$className}}& operator=(const {{$className}}&) = delete;

  void RegisterWithDBusObject(brillo::dbus_utils::DBusObject* object) {
    brillo::dbus_utils::DBusInterface* itf =
        object->AddOrGetInterface("{{.Name}}");
        {{- /* TODO(chromium:983008): Add register interface */}}
  }
  {{- /* TODO(chromium:983008): Add send signal methods */}}
  {{- /* TODO(chromium:983008): Add property method implementation */}}
  {{- if $introspect.Name}}

  static dbus::ObjectPath GetObjectPath() {
    return dbus::ObjectPath{"{{$introspect.Name}}"};
  }
  {{- end}}

  static const char* GetIntrospectionXml() {
    return
        "  <interface name=\"{{.Name}}\">\n"
        {{- /* TODO(chromium:983008): Generate Quoted Introspection For Interface */}}
        "  </interface>\n";
  }

 private:
  {{- /* TODO(chromium:983008): Add Signal Data Members */}}
  {{- /* TODO(chromium:983008): Add Property Data Members */}}
  {{- if .Methods}}
  {{$itfName}}* interface_;  // Owned by container of this adapter.
  {{- end}}
};

{{range extractNameSpaces .Name | reverse -}}
}  // namespace {{.}}
{{end -}}
{{end}}{{end -}}
#endif  // {{.HeaderGuard}}
`

// Generate prints an interface definition and an interface adaptor for each interface in introspects.
func Generate(introspects []introspect.Introspection, f io.Writer, outputFilePath string) error {
	tmpl, err := template.New("adaptor").Funcs(funcMap).Parse(templateText)
	if err != nil {
		return err
	}
	var headerGuard = genutil.GenerateHeaderGuard(outputFilePath)
	return tmpl.Execute(f, templateArgs{introspects, headerGuard})
}
