# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "bootloader",
    srcs = ["bootloader.cpp"],
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//simu:simu",
        "//emul:emul",
        "//core:core",
        "//mem:mem",
    ]
)

cc_binary(
    name = "desesc",
    srcs = [
        "desesc.cpp",
    ],
    deps = [
        ":bootloader",
    ],
)

sh_test(
    name = "goldrun_test",
    size = "small",
    srcs = ["goldrun_test.sh"],
    data = [
        ":desesc",
        "//conf:goldrun_data",
    ],
)
