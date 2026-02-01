# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "emul",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*_test*.cpp", "*_bench*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core:core",
        "@dromajo",
    ]
)

cc_binary(
    name = "dromajo_test",
    srcs = [
        "dromajo_test.cpp",
    ],
    deps = [
        ":emul",
    ],
)

cc_binary(
    name = "emul_dromajo_test",
    srcs = [
        "emul_dromajo_test.cpp",
        "emul_dromajo.cpp",
    ],
    deps = [
        ":emul",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main"
    ],
)

cc_binary(
    name = "emul_dromajo_bench",
    srcs = [
        "emul_dromajo_bench.cpp",
        "emul_dromajo.cpp",
    ],
    deps = [
        ":emul",
        "@com_google_benchmark//:benchmark",
    ],
)
