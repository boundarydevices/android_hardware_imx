package alsa

import (
	"android/soong/android"
	"android/soong/cc"
	"strconv"
)

func init() {
	android.RegisterModuleType("audio_primary_defaults", alsaDefaultsFactory)
}

func alsaDefaultsFactory() android.Module {
	module := cc.DefaultsFactory()
	android.AddLoadHook(module, alsaDefaults)
	return module
}

func alsaDefaults(ctx android.LoadHookContext) {
	var cppflags []string
	type props struct {
		Cflags []string
	}

	p := &props{}
	cppflags = append(cppflags, "-DANDROID_SDK_VERSION="+strconv.Itoa(ctx.AConfig().PlatformSdkVersion().FinalOrFutureInt()))
	p.Cflags = cppflags
	ctx.AppendProperties(p)
}
