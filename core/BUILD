# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "core",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*_test*.cpp", "*_bench*.cpp"],
    ),
    hdrs = glob(["*.hpp", "*.h"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
      "@abseil-cpp//absl/container:flat_hash_map",
      "@abseil-cpp//absl/container:node_hash_map",
      "@abseil-cpp//absl/container:flat_hash_set",
      "@abseil-cpp//absl/container:node_hash_set",
      "@abseil-cpp//absl/container:btree",
      "@abseil-cpp//absl/strings",
      "@fmt",
      "@iassert",
      "@toml11",
    ]
)

cc_test(
    name = "config_test",
    srcs = [
        "config_test.cpp",
    ],
    deps = [
        ":core",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "callback_bench",
    srcs = [
        "callback_bench.cpp",
    ],
    deps = [
        ":core",
        "@com_google_benchmark//:benchmark",
    ],
)

cc_test(
    name = "cachecore_bench",
    srcs = [
        "cachecore_bench.cpp",
    ],
    deps = [
        ":core",
        "@com_google_benchmark//:benchmark",
    ],
)

