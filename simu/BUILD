# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "simu",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*_test*.cpp", "*_bench*.cpp"],
    ),
    hdrs = glob(["*.hpp", "*.h"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@fmt",
        "@iassert",
        "@toml11",
        "//core:core",
    ]
)

