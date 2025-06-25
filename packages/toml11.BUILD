# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "toml11",
    hdrs = glob(["include/**"]),
    defines = ["TOML11_COMPILE_SOURCES"],
    srcs = glob(["src/*.cpp"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
)
