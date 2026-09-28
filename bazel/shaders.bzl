"""Compile GLSL shaders to SPIR-V and embed them into a C++ library.

Uses the platform-matched prebuilt glslang and plain actions (no
genrule/bash), so it works with the MSVC toolchain on Windows.
"""

load("@rules_cc//cc:defs.bzl", "cc_library")

def _spirv_embed_impl(ctx):
    spvs = []
    for src in ctx.files.srcs:
        spv = ctx.actions.declare_file(ctx.label.name + "/" + src.basename + ".spv")
        args = ctx.actions.args()
        args.add("-V")
        args.add("--target-env", "vulkan1.1")
        args.add("-o", spv)
        args.add(src)
        ctx.actions.run(
            executable = ctx.executable.glslang,
            arguments = [args],
            inputs = [src] + ctx.files.hdrs,
            outputs = [spv],
            mnemonic = "GlslToSpirv",
            progress_message = "Compiling shader %{input}",
        )
        spvs.append(spv)

    args = ctx.actions.args()
    args.add("--namespace=" + ctx.attr.namespace)
    args.add("--header=" + ctx.outputs.header.path)
    args.add("--source=" + ctx.outputs.source.path)
    args.add("--include=" + ctx.attr.include)
    args.add_all(spvs)
    ctx.actions.run(
        executable = ctx.executable._embed,
        arguments = [args],
        inputs = spvs,
        outputs = [ctx.outputs.header, ctx.outputs.source],
        mnemonic = "EmbedSpirv",
        progress_message = "Embedding SPIR-V into %{output}",
    )
    return [DefaultInfo(files = depset([ctx.outputs.header, ctx.outputs.source]))]

_spirv_embed = rule(
    implementation = _spirv_embed_impl,
    attrs = {
        "srcs": attr.label_list(allow_files = [".vert", ".frag", ".comp", ".geom"]),
        "hdrs": attr.label_list(allow_files = True),
        "namespace": attr.string(default = "shaders"),
        "include": attr.string(mandatory = True),
        "header": attr.output(mandatory = True),
        "source": attr.output(mandatory = True),
        "glslang": attr.label(
            mandatory = True,
            allow_single_file = True,
            executable = True,
            cfg = "exec",
        ),
        "_embed": attr.label(
            default = "//tools/embed:embed_spirv",
            executable = True,
            cfg = "exec",
        ),
    },
)

def spirv_library(name, srcs, hdrs = [], namespace = "shaders", glslang = None, **kwargs):
    """Compiles each GLSL file in `srcs` to SPIR-V and embeds the result.

    `hdrs` are GLSL files pulled in with #include (GL_GOOGLE_include_directive).
    Generates `<name>.h` declaring `std::span<const uint32_t> <namespace>::<id>()`
    for each shader, where id is the file name with '.' replaced by '_'.
    """
    _spirv_embed(
        name = name + "_embed",
        srcs = srcs,
        hdrs = hdrs,
        glslang = glslang,
        namespace = namespace,
        include = native.package_name() + "/" + name + ".h",
        header = name + ".h",
        source = name + ".cc",
    )

    cc_library(
        name = name,
        hdrs = [name + ".h"],
        srcs = [name + ".cc"],
        **kwargs
    )
