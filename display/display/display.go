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

package display

import (
        "android/soong/android"
        "android/soong/cc"
        "strings"
        "github.com/google/blueprint/proptools"
)

func init() {
    android.RegisterModuleType("libionallocator", libionallocatorDefaultsFactory)
    android.RegisterModuleType("libfsldisplay", libfsldisplayDefaultsFactory)
}

func libionallocatorDefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, libionallocatorDefaults)
    return module
}

func libionallocatorDefaults(ctx android.LoadHookContext) {
    var Cppflags []string
    type props struct {
        cppflags []string
        Target struct {
                Android struct {
                        Enabled *bool
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
    if ctx.Config().VendorConfig("IMXPLUGIN").String("CFG_SECURE_DATA_PATH") == "y" {
        Cppflags = append(Cppflags, "-DCFG_SECURE_DATA_PATH")
    }
    p.cppflags = Cppflags
    ctx.AppendProperties(p)
}

func libfsldisplayDefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, libfsldisplayDefaults)
    return module
}

func libfsldisplayDefaults(ctx android.LoadHookContext) {
    var Cppflags []string
    var Cflags []string
    type props struct {
        cppflags []string
        cflags []string
        Target struct {
                Android struct {
                        Enabled *bool
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
    if ctx.Config().VendorConfig("IMXPLUGIN").String("NUM_FRAMEBUFFER_SURFACE_BUFFERS") != "" {
         Cflags = append(Cflags, "-DNUM_FRAMEBUFFER_SURFACE_BUFFERS= " + ctx.Config().VendorConfig("IMXPLUGIN").String("NUM_FRAMEBUFFER_SURFACE_BUFFERS"))
    }
    if ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_SOC_TYPE") == "IMX8Q" {
        Cppflags = append(Cppflags, "-DWORKAROUND_DOWNSCALE_LIMITATION")
    }
    if ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_SOC_CLASS") == "IMX8" {
        Cflags = append(Cflags, "-DIMX8")
    }
    if ctx.Config().VendorConfig("IMXPLUGIN").String("OARD_SOC_TYPE") == "IMX8MQ" {
        Cflags = append(Cflags, "-DFRAMEBUFFER_COMPRESSION")
    }
    if ctx.Config().VendorConfig("IMXPLUGIN").Bool("AVE_FSL_IMX_GPU3D") {
        Cflags = append(Cflags,"-DUSE_SW_OPENGL")
    }
    if ctx.Config().VendorConfig("IMXPLUGIN").String("FG_SECURE_DATA_PATH") == "y" {
        Cppflags = append(Cppflags, "-DCFG_SECURE_DATA_PATH")
    }
    p.cflags = Cflags
    p.cppflags = Cppflags
    ctx.AppendProperties(p)
}

