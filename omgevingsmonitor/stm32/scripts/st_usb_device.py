# Adds ST's STM32_USB_Device_Library (Core + CDC class) from the installed
# framework-stm32cubel0 package to the build, so no ST sources live in this repo.
# Only used by the ESP flashing bridge (bridge.c).
import os
Import("env")

fw = env.PioPlatform().get_package_dir("framework-stm32cubel0")
usb = os.path.join(fw, "Middlewares", "ST", "STM32_USB_Device_Library")
core_inc = os.path.join(usb, "Core", "Inc")
cdc_inc = os.path.join(usb, "Class", "CDC", "Inc")

env.Append(CPPPATH=[core_inc, cdc_inc])
env.BuildSources(
    os.path.join("$BUILD_DIR", "st_usb_device"),
    usb,
    src_filter=[
        "-<*>",
        "+<Core/Src/usbd_core.c>",
        "+<Core/Src/usbd_ctlreq.c>",
        "+<Core/Src/usbd_ioreq.c>",
        "+<Class/CDC/Src/usbd_cdc.c>",
    ],
)
