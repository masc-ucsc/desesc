# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

workspace(name = "desesc")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# toml11
http_archive(
    name = "toml11",
    build_file = "//ext:toml11.BUILD",
    sha256 = "e3f956df9e2c162cddf7e4ef83f35de9e2771b8e6997faa2bb3ceae5ee9c2757",
    strip_prefix = "toml11-4.2.0",
    urls = [
        "https://github.com/ToruNiina/toml11/archive/v4.2.0.zip",
    ],
)

# Dromajo
http_archive(
    name = "dromajo",
    build_file = "//ext:dromajo.BUILD",
    patches = ["//ext:dromajo.patch"],
    sha256 = "80d14d855a4f373496697a7122abafe582754dde74286491288e2c0d937890c3",
    strip_prefix = "dromajo-6f1f94bf32ddd71387d8c85e4e6cac0888615684",
    urls = [
      "https://github.com/masc-ucsc/dromajo/archive/6f1f94bf32ddd71387d8c85e4e6cac0888615684.zip",
    ],
)

# superbp
http_archive(
    name = "superbp",
    sha256 = "7985cd322e539c82cda59834aa83fe63695d8edd56395871ff0ad40eb5519b2b",
    strip_prefix = "superbp-8bd3674f18c7458b75be71796ab9906beff71a6e",
    urls = [
      "https://github.com/masc-ucsc/superbp/archive/8bd3674f18c7458b75be71796ab9906beff71a6e.zip",
    ],
)

# libelf (needed by dromajo in OSX)
http_archive(
    name = "libelf",
    build_file = "//ext:libelf.BUILD",
    sha256 = "c0627b45c29a151e4e1105988ad7ce9bf83b52cbbca0a1db06c7fcad69b85c4b",
    strip_prefix = "libelf-ba3c81450b91d1935fff01bae191a59d7653d2a5",
    urls = [
        "https://github.com/masc-ucsc/libelf/archive/ba3c81450b91d1935fff01bae191a59d7653d2a5.zip",
    ],
)

