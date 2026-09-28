load("@rules_cc//cc:defs.bzl", "cc_import", "cc_library")

package(default_visibility = ["//visibility:public"])

_LIBS = [
    "skia",
    "skcms",
    "freetype2",
    "harfbuzz",
    "png",
    "jpeg",
    "webp",
    "webp_sse41",
    "zlib",
    "expat",
]

[
    cc_import(
        name = lib + "_import",
        static_library = "out/Release-x64/lib" + lib + ".a",
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
    defines = ["SK_RELEASE"],
    includes = ["."],
    linkopts = ["-ldl", "-lpthread", "-lm"],
    deps = [":" + lib + "_import" for lib in _LIBS],
)
