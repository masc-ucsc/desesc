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

cc_test(
    name = "cache_test",
    srcs = [
        "cache_test.cpp",
    ],
    deps = [
        ":mem",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "store_buffer_test",
    srcs = [
        "store_buffer_test.cpp",
    ],
    deps = [
        "//simu:simu",
        ":mem",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main"
    ],
)

cc_test(
    name = "resource_test",
    srcs = [
        "resource_test.cpp",
    ],
    deps = [
        "//simu:simu",
        ":mem",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main"
    ],
)
