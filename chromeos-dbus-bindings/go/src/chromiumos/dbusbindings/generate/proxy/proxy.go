// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package proxy outputs client-side bindings classes based on introspects.
package proxy

import (
	"io"
	"text/template"

	"chromiumos/dbusbindings/generate/genutil"
	"chromiumos/dbusbindings/introspect"
	"chromiumos/dbusbindings/serviceconfig"
)

var funcMap = template.FuncMap{
	"makeProxyName":     genutil.MakeProxyName,
	"extractNameSpaces": genutil.ExtractNameSpaces,
	"reverse":           genutil.Reverse,
}

const (
	templateText = `// Automatic generation of D-Bus interfaces:
{{range .Introspects}}{{range .Interfaces -}}
//  - {{.Name}}
{{end}}{{end -}}

#ifndef {{.HeaderGuard}}
#define {{.HeaderGuard}}
#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <brillo/any.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <brillo/dbus/dbus_property.h>
#include <brillo/dbus/dbus_signal_handler.h>
#include <brillo/dbus/file_descriptor.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_manager.h>
#include <dbus/object_path.h>
#include <dbus/object_proxy.h>

{{if .ObjectManagerName -}}
{{range extractNameSpaces .ObjectManagerName -}}
namespace {{.}} {
{{end -}}
class {{makeProxyName .ObjectManagerName}};
{{range extractNameSpaces .ObjectManagerName | reverse -}}
}  // namespace {{.}}
{{end}}
{{end -}}
{{range $introspect := .Introspects}}{{range .Interfaces -}}
{{- /* TODO(crbug.com/983008): Convert GenerateInterfaceProxyInterface */ -}}
{{- /* TODO(crbug.com/983008): Convert GenerateInterfaceProxy */ -}}
{{end}}{{end -}}
{{- /* TODO(crbug.com/983008): Convert ObjectManager::GenerateProxy */ -}}
#endif  // {{.HeaderGuard}}
`
)

// Generate outputs the header file containing proxy interfaces into f.
// outputFilePath is used to make a unique header guard.
func Generate(introspects []introspect.Introspection, f io.Writer, outputFilePath string, config serviceconfig.Config) error {
	tmpl, err := template.New("proxy").Funcs(funcMap).Parse(templateText)
	if err != nil {
		return err
	}

	headerGuard := genutil.GenerateHeaderGuard(outputFilePath)
	return tmpl.Execute(f, struct {
		Introspects       []introspect.Introspection
		HeaderGuard       string
		ObjectManagerName string
	}{
		Introspects:       introspects,
		HeaderGuard:       headerGuard,
		ObjectManagerName: config.ObjectManager.Name,
	})
}
