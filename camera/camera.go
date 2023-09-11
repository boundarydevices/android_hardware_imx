/*
 *  Copyright 2023 NXP.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

package camera

import (
	"android/soong/android"
	"android/soong/cc"
	"github.com/google/blueprint/proptools"
	"strconv"
	"strings"
)

func init() {
	android.RegisterModuleType("imx_camera_defaults", cameraDefaultsFactory)
}

func cameraDefaultsFactory() android.Module {
	module := cc.DefaultsFactory()
	android.AddLoadHook(module, cameraDefaults)
	return module
}

func cameraDefaults(ctx android.LoadHookContext) {
	var cppflags []string
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
	if strings.Contains(board, "imx") {
		p.Target.Android.Enabled = proptools.BoolPtr(true)
	} else {
		p.Target.Android.Enabled = proptools.BoolPtr(false)
	}
	cppflags = append(cppflags, "-DANDROID_SDK_VERSION="+strconv.Itoa(ctx.AConfig().PlatformSdkVersion().FinalOrFutureInt()))
	if ctx.Config().VendorConfig("IMXPLUGIN").String("TARGET_GRALLOC_VERSION") == "v4" {
		cppflags = append(cppflags, "-DGRALLOC_VERSION=4")
	}
	p.Target.Android.Cppflags = cppflags
	ctx.AppendProperties(p)
}
