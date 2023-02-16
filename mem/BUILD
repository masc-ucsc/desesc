# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "mem",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*_test*.cpp", "*_bench*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//simu:simu",
        "//core:core",
    ]
)

cc_test(
    name = "mshr_test",
    srcs = [
        "mshr_test.cpp",
    ],
    deps = [
        ":mem",
        "@com_google_googletest//:gtest_main",
    ],
)

