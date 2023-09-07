// Copyright 2019 NXP
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//	http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ANDROIDMK TRANSLATION ERROR: unsupported conditional
// ifeq ($(findstring imx, $(TARGET_BOARD_PLATFORM)), imx)
package hwcomposer_v20

import (
	"android/soong/android"
	"android/soong/cc"
	"github.com/google/blueprint/proptools"
	"strings"
)

func init() {
	android.RegisterModuleType("hwcomposer_v20_defaults", hwcomposer_v20DefaultsFactory)
}

func hwcomposer_v20DefaultsFactory() android.Module {
	module := cc.DefaultsFactory()
	android.AddLoadHook(module, hwcomposer_v20Defaults)
	return module
}

func hwcomposer_v20Defaults(ctx android.LoadHookContext) {
	type props struct {
		Target struct {
			Android struct {
				Enabled  *bool
				Cppflags []string
			}
		}
	}

	p := &props{}
	var board string = ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_PLATFORM")
	var hwcomposer_version string = ctx.Config().VendorConfig("IMXPLUGIN").String("TARGET_HWCOMPOSER_VERSION")
	if strings.Contains(board, "imx") && hwcomposer_version == "v2.0" {
		p.Target.Android.Enabled = proptools.BoolPtr(true)
	} else {
		p.Target.Android.Enabled = proptools.BoolPtr(false)
	}
	if ctx.Config().VendorConfig("IMXPLUGIN").String("TARGET_GRALLOC_VERSION") == "v4" {
		p.Target.Android.Cppflags = append(p.Target.Android.Cppflags, "-DGRALLOC_VERSION=4")
	}
	ctx.AppendProperties(p)
}
