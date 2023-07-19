// Copyright 2023 NXP
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

package ext_cam_aidl

import (
        "android/soong/android"
        "android/soong/cc"
        "strings"
)

func init() {
    android.RegisterModuleType("imx_ext_cam_aidl_defaults", extCamDefaultsFactory)
}

func extCamDefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, extCamDefaults)
    return module
}

func extCamDefaults(ctx android.LoadHookContext) {
    var Cflags []string
    type props struct {
        Target struct {
                Android struct {
                        Enabled *bool
                        Cflags []string
                }
        }
    }
    p := &props{}
    var board string = ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_SOC_TYPE")
    if (strings.Contains(board, "IMX8Q") || strings.Contains(board, "IMX8MP") || strings.Contains(board, "IMX8MQ") || strings.Contains(board, "IMX8MM")) {
        if (strings.Contains(board, "IMX8Q")) {
            Cflags = append(Cflags, "-DAMPHION_V4L2")
        } else if (strings.Contains(board, "IMX8MQ")) {
            Cflags = append(Cflags, "-DHANTRO_V4L2")
        }
    }
    p.Target.Android.Cflags = Cflags
    ctx.AppendProperties(p)
}

