load("@rules_cc//cc:defs.bzl", "cc_import", "cc_library")

package(default_visibility = ["//visibility:public"])

_LIBS = [
    "skia",
    "skcms",
    "freetype2",
    "harfbuzz",
    "libpng",
    "zlib",
    "libjpeg",
    "libwebp",
    "libwebp_sse41",
    "wuffs",
    "expat",
]

[
    cc_import(
        name = lib + "_import",
        static_library = "out/Release-x64/" + lib + ".lib",
    )
    for lib in _LIBS
]

cc_library(
    name = "skia",
    hdrs = glob([
        "include/**/*.h",
        "src/**/*.h",
        "modules/skcms/**/*.h",
    ]),
    # The prebuilt library is a release build; make headers agree on struct layouts.
    defines = ["SK_RELEASE"],
    includes = ["."],
    linkopts = [
        "user32.lib",
        "gdi32.lib",
        "ole32.lib",
        "advapi32.lib",
        "dwrite.lib",
        "usp10.lib",
        "fontsub.lib",
        "windowscodecs.lib",
    ],
    deps = [":" + lib + "_import" for lib in _LIBS],
)
