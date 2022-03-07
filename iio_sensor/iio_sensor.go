package iio_sensor

import (
        "android/soong/android"
        "android/soong/cc"
        "strings"
        "github.com/google/blueprint/proptools"
)

func init() {
    android.RegisterModuleType("iio_sensor_defaults", iio_sensorDefaultsFactory)
}

func iio_sensorDefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, iio_sensorDefaults)
    return module
}

func iio_sensorDefaults(ctx android.LoadHookContext) {
    type props struct {
        Target struct {
                Android struct {
                        Enabled *bool
                        Cppflags []string
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

    if ctx.Config().VendorConfig("IMXPLUGIN").Bool("BOARD_USE_LEGACY_SENSOR") {
         p.Target.Android.Enabled = proptools.BoolPtr(true)
         p.Target.Android.Srcs = append(p.Target.Android.Srcs, "AccMagSensor.cpp")
         p.Target.Android.Srcs = append(p.Target.Android.Srcs, "AnglvelSensor.cpp")
         p.Target.Android.Srcs = append(p.Target.Android.Srcs, "LightSensor.cpp")
         p.Target.Android.Srcs = append(p.Target.Android.Srcs, "PressureSensor.cpp")
         p.Target.Android.Cppflags = append(p.Target.Android.Cppflags, "-DCONFIG_LEGACY_SENSOR")
    } else if ctx.Config().VendorConfig("IMXPLUGIN").Bool("BOARD_USE_SENSOR_PEDOMETER") {
         p.Target.Android.Enabled = proptools.BoolPtr(true)
         p.Target.Android.Srcs = append(p.Target.Android.Srcs, "StepCounterSensor.cpp")
         p.Target.Android.Cppflags = append(p.Target.Android.Cppflags, "-DCONFIG_SENSOR_PEDOMETER")
    }

    ctx.AppendProperties(p)
}
