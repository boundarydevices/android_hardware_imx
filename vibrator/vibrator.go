package vibrator

import (
	"android/soong/android"
	"android/soong/cc"
)

func init() {
	android.RegisterModuleType("vibrator_defaults", vibratorDefaultsFactory)
}

func vibratorDefaultsFactory() android.Module {
	module := cc.DefaultsFactory()
	android.AddLoadHook(module, vibratorDefaults)
	return module
}

func vibratorDefaults(ctx android.LoadHookContext) {
	type props struct {
		Target struct {
			Android struct {
				Shared_libs []string
			}
		}
	}

	p := &props{}

	var version string = ctx.AConfig().PlatformVersionName()
	if version == "12" {
		p.Target.Android.Shared_libs = append(p.Target.Android.Shared_libs, "android.hardware.vibrator-V2-ndk_platform")
	} else {
		p.Target.Android.Shared_libs = append(p.Target.Android.Shared_libs, "android.hardware.vibrator-V2-ndk")
	}

	ctx.AppendProperties(p)
}
