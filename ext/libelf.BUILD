# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "libelf",
    srcs = glob(["libelf/*.c"]),
    hdrs = glob(["libelf/*.h"]),
    copts = [
        "-w",
        "-O2",
    ],
    includes = [".", "libelf"],
    visibility = ["//visibility:public"],
)
