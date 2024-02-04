package iio_sensor

import (
	"android/soong/android"
	"android/soong/cc"
	"github.com/google/blueprint/proptools"
	"strings"
)

func init() {
	android.RegisterModuleType("iio_sensor_defaults", iio_sensorDefaultsFactory)
}

func iio_sensorDefaultsFactory() android.Module {
	module := cc.DefaultsFactory()
	android.AddLoadHook(module, iio_sensorDefaults)
	return module
}

func iio_sensorDefaults(ctx android.LoadHookContext) {
	type props struct {
		Target struct {
			Android struct {
				Enabled  *bool
				Cppflags []string
				Srcs     []string
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

	ctx.AppendProperties(p)
}
