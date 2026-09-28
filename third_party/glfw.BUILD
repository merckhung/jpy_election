load("@rules_cc//cc:defs.bzl", "cc_library")

COMMON_SRCS = [
    "src/context.c",
    "src/init.c",
    "src/input.c",
    "src/monitor.c",
    "src/platform.c",
    "src/vulkan.c",
    "src/window.c",
    "src/egl_context.c",
    "src/osmesa_context.c",
    "src/null_init.c",
    "src/null_monitor.c",
    "src/null_window.c",
    "src/null_joystick.c",
]

WIN32_SRCS = [
    "src/win32_time.c",
    "src/win32_thread.c",
    "src/win32_module.c",
    "src/win32_init.c",
    "src/win32_joystick.c",
    "src/win32_monitor.c",
    "src/win32_window.c",
    "src/wgl_context.c",
]

LINUX_SRCS = [
    "src/posix_time.c",
    "src/posix_thread.c",
    "src/posix_module.c",
    "src/posix_poll.c",
    "src/linux_joystick.c",
    "src/x11_init.c",
    "src/x11_monitor.c",
    "src/x11_window.c",
    "src/xkb_unicode.c",
    "src/glx_context.c",
]

cc_library(
    name = "glfw",
    srcs = COMMON_SRCS + select({
        "@platforms//os:windows": WIN32_SRCS,
        "//conditions:default": LINUX_SRCS,
    }) + glob(["src/*.h"]),
    hdrs = [
        "include/GLFW/glfw3.h",
        "include/GLFW/glfw3native.h",
    ],
    includes = ["include"],
    local_defines = select({
        "@platforms//os:windows": ["_GLFW_WIN32"],
        "//conditions:default": [
            "_GLFW_X11",
            "_DEFAULT_SOURCE",
        ],
    }),
    linkopts = select({
        "@platforms//os:windows": [
            "-DEFAULTLIB:user32.lib",
            "-DEFAULTLIB:gdi32.lib",
            "-DEFAULTLIB:shell32.lib",
        ],
        "//conditions:default": [
            "-ldl",
            "-lpthread",
            "-lX11",
        ],
    }),
    visibility = ["//visibility:public"],
)
