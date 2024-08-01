/*
 *  Copyright 2023-2024 NXP.
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
				Enabled      *bool
				Cppflags     []string
				Srcs         []string
				Shared_libs  []string
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
	cppflags = append(cppflags, "-DANDROID_SDK_VERSION="+strconv.Itoa(ctx.AConfig().PlatformSdkVersion().FinalOrFutureInt()))
	if ctx.Config().VendorConfig("IMXPLUGIN").String("TARGET_GRALLOC_VERSION") == "v4" {
		cppflags = append(cppflags, "-DGRALLOC_VERSION=4")
	}

	if ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_SOC_TYPE") == "IMX95" {
		if ctx.Config().VendorConfig("IMXPLUGIN").String("MEDIA_PIPELINE") == "NEOISP" {
			p.Target.Android.Srcs = append(p.Target.Android.Srcs, "./camera-hal-v2/CameraProviderHWLImpl.cpp")
			p.Target.Android.Srcs = append(p.Target.Android.Srcs, "./camera-hal-v2/CameraDeviceHWLImpl.cpp")
			p.Target.Android.Srcs = append(p.Target.Android.Srcs, "./camera-hal-v2/CameraDeviceSessionHWLImpl.cpp")
			p.Target.Android.Include_dirs = append(p.Target.Android.Include_dirs, "vendor/nxp-opensource/imx/camera/camera-hal-v2")
			p.Target.Android.Include_dirs = append(p.Target.Android.Include_dirs, "vendor/nxp-opensource/libcamera/prebuilt-android/include")
		} else {
			p.Target.Android.Srcs = append(p.Target.Android.Srcs, "./camera-hal/CameraProviderHWLImpl.cpp")
			p.Target.Android.Srcs = append(p.Target.Android.Srcs, "./camera-hal/CameraDeviceHWLImpl.cpp")
			p.Target.Android.Srcs = append(p.Target.Android.Srcs, "./camera-hal/CameraDeviceSessionHWLImpl.cpp")
			p.Target.Android.Include_dirs = append(p.Target.Android.Include_dirs, "vendor/nxp-opensource/imx/camera/camera-hal")
			p.Target.Android.Include_dirs = append(p.Target.Android.Include_dirs, "vendor/nxp-opensource/libcamera/build/include")
		}
		p.Target.Android.Shared_libs = append(p.Target.Android.Shared_libs, "libcamera")
		p.Target.Android.Shared_libs = append(p.Target.Android.Shared_libs, "libcamera-base")
		p.Target.Android.Include_dirs = append(p.Target.Android.Include_dirs, "vendor/nxp-opensource/libcamera/include")
	} else {
		cppflags = append(cppflags, "-DISIMX8=1")
		p.Target.Android.Srcs = append(p.Target.Android.Srcs, "./camera/CameraProviderHWLImpl.cpp")
		p.Target.Android.Srcs = append(p.Target.Android.Srcs, "./camera/CameraDeviceHWLImpl.cpp")
		p.Target.Android.Srcs = append(p.Target.Android.Srcs, "./camera/CameraDeviceSessionHWLImpl.cpp")
		p.Target.Android.Srcs = append(p.Target.Android.Srcs, "./camera/VideoStream.cpp")
		p.Target.Android.Srcs = append(p.Target.Android.Srcs, "./camera/MMAPStream.cpp")
		p.Target.Android.Srcs = append(p.Target.Android.Srcs, "./camera/DMAStream.cpp")
		p.Target.Android.Srcs = append(p.Target.Android.Srcs, "./camera/UvcStream.cpp")
		p.Target.Android.Srcs = append(p.Target.Android.Srcs, "./camera/ISPCameraDeviceHWLImpl.cpp")
		p.Target.Android.Srcs = append(p.Target.Android.Srcs, "./camera/ISPWrapper.cpp")
		p.Target.Android.Include_dirs = append(p.Target.Android.Include_dirs, "vendor/nxp-opensource/imx/camera/camera")
	}

	p.Target.Android.Cppflags = cppflags
	ctx.AppendProperties(p)
}
