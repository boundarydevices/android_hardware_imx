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
    android.RegisterModuleType("libionallocator_defaults", libionallocatorDefaultsFactory)
    android.RegisterModuleType("libfsldisplay_defaults", libfsldisplayDefaultsFactory)
}

func libionallocatorDefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, libionallocatorDefaults)
    return module
}

func libionallocatorDefaults(ctx android.LoadHookContext) {
    type props struct {
        Target struct {
                Android struct {
                        Enabled *bool
                        Cppflags []string
                        Static_libs []string
                        Include_dirs []string
                        Srcs []string
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
        p.Target.Android.Cppflags = append(p.Target.Android.Cppflags, "-DCFG_SECURE_DATA_PATH")
    }
    if ctx.Config().VendorConfig("IMXPLUGIN").Bool("ENABLE_DMABUF_HEAP") {
        p.Target.Android.Srcs = append(p.Target.Android.Srcs, "DmaHeapAllocator.cpp")
        p.Target.Android.Static_libs = append(p.Target.Android.Static_libs, "libdmabufheap")
        p.Target.Android.Cppflags = append(p.Target.Android.Cppflags, "-DENABLE_DMABUF_HEAP")
        p.Target.Android.Include_dirs = append(p.Target.Android.Include_dirs, "system/memory/libdmabufheap/include")
    }

    ctx.AppendProperties(p)
}

func libfsldisplayDefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, libfsldisplayDefaults)
    return module
}

func libfsldisplayDefaults(ctx android.LoadHookContext) {
    type props struct {
        Target struct {
                Android struct {
                        Enabled *bool
                        Cflags []string
                        Cppflags []string
                        Shared_libs []string
                        Include_dirs []string
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
        p.Target.Android.Cflags = append(p.Target.Android.Cflags, "-DNUM_FRAMEBUFFER_SURFACE_BUFFERS=" + ctx.Config().VendorConfig("IMXPLUGIN").String("NUM_FRAMEBUFFER_SURFACE_BUFFERS"))
    }
    if ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_SOC_TYPE") == "IMX8Q" {
        p.Target.Android.Cppflags = append(p.Target.Android.Cppflags, "-DWORKAROUND_DOWNSCALE_LIMITATION")
        p.Target.Android.Cppflags = append(p.Target.Android.Cppflags, "-DWORKAROUND_DPU_ALPHA_BLENDING")
        p.Target.Android.Cppflags = append(p.Target.Android.Cppflags, "-DWORKAROUND_DISPLAY_UNDERRUN")
        p.Target.Android.Cppflags = append(p.Target.Android.Cppflags, "-DUSE_DPU_HWC")
    }
    if ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_SOC_CLASS") == "IMX8" {
        p.Target.Android.Cflags = append(p.Target.Android.Cflags, "-DIMX8")
    }
    if ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_SOC_TYPE") == "IMX8MP" {
        p.Target.Android.Cflags = append(p.Target.Android.Cflags, "-DHAVE_UNMAPPED_HEAP")
    }
    if ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_SOC_TYPE") == "IMX8MQ" {
        p.Target.Android.Cflags = append(p.Target.Android.Cflags, "-DFRAMEBUFFER_COMPRESSION")
        p.Target.Android.Cflags = append(p.Target.Android.Cflags, "-DWORKAROUND_DOWNSCALE_LIMITATION_DCSS")
        p.Target.Android.Cflags = append(p.Target.Android.Cflags, "-DHAVE_UNMAPPED_HEAP")
    }
    if ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_SOC_TYPE") == "IMX8MM" {
        p.Target.Android.Cflags = append(p.Target.Android.Cflags, "-DWORKAROUND_VIRTUAL_DISPLAY_FLICKER")
    }
    if ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_SOC_TYPE") == "IMX8ULP" {
        p.Target.Android.Cflags = append(p.Target.Android.Cflags, "-DWORKAROUND_DCNANO_BGRX")
    }
    if ctx.Config().VendorConfig("IMXPLUGIN").Bool("HAVE_FSL_IMX_GPU3D") {
        p.Target.Android.Cflags = append(p.Target.Android.Cflags,"-DUSE_SW_OPENGL")
    }
    if ctx.Config().VendorConfig("IMXPLUGIN").String("CFG_SECURE_DATA_PATH") == "y" {
        p.Target.Android.Cppflags = append(p.Target.Android.Cppflags, "-DCFG_SECURE_DATA_PATH")
    }
    if ctx.Config().VendorConfig("IMXPLUGIN").String("TARGET_GRALLOC_VERSION") == "v4" {
        p.Target.Android.Cppflags = append(p.Target.Android.Cppflags, "-DGRALLOC_VERSION=4")
    }
    if ctx.Config().VendorConfig("IMXPLUGIN").Bool("ENABLE_DMABUF_HEAP") {
        p.Target.Android.Shared_libs = append(p.Target.Android.Shared_libs, "libdmabufheap")
        p.Target.Android.Include_dirs = append(p.Target.Android.Include_dirs, "system/memory/libdmabufheap/include")
    }
    ctx.AppendProperties(p)
}
