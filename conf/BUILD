# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

# Expose gold run test data files
filegroup(
    name = "goldrun_data",
    srcs = [
        "dhrystone.riscv",
        "goldrun1_desesc.result",
        "goldrun1_desesc.toml",
        "goldrun1_kanata_log.result",
    ],
    visibility = ["//visibility:public"],
)

# Expose all config files for general use
filegroup(
    name = "configs",
    srcs = glob(["*.toml"]),
    visibility = ["//visibility:public"],
)
