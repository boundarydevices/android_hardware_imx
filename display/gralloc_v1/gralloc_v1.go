// Copyright 2019 NXP
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package gralloc_v1

import (
	"android/soong/android"
	"android/soong/cc"
	"github.com/google/blueprint/proptools"
	"strings"
)

func init() {
	android.RegisterModuleType("gralloc_v1_defaults", gralloc_v1DefaultsFactory)
}

func gralloc_v1DefaultsFactory() android.Module {
	module := cc.DefaultsFactory()
	android.AddLoadHook(module, gralloc_v1Defaults)
	return module
}

func gralloc_v1Defaults(ctx android.LoadHookContext) {
	type props struct {
		Target struct {
			Android struct {
				Enabled *bool
				Cflags  []string
			}
		}
	}
	p := &props{}
	var board string = ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_PLATFORM")
	if strings.Contains(board, "imx") && ctx.Config().VendorConfig("IMXPLUGIN").String("TARGET_GRALLOC_VERSION") == "v1" {
		p.Target.Android.Enabled = proptools.BoolPtr(true)
	} else {
		p.Target.Android.Enabled = proptools.BoolPtr(false)
	}
	if ctx.Config().VendorConfig("IMXPLUGIN").Bool("TARGET_USE_PAN_DISPLAY") {
		p.Target.Android.Cflags = append(p.Target.Android.Cflags, "-DUSE_PAN_DISPLAY=1")
	}
	ctx.AppendProperties(p)
}
