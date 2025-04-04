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

# iassert
http_archive(
    name = "iassert",
    sha256 = "c6bf66a76d5a1de57c45dba137c9b51ab3b4f3a31e5de9e3c3496d7d36a128f8",
    strip_prefix = "iassert-5c18eb082262532f621a23023f092f4119a44968",
    urls = [
        "https://github.com/masc-ucsc/iassert/archive/5c18eb082262532f621a23023f092f4119a44968.zip",
    ],
)

# Dromajo
http_archive(
    name = "dromajo",
    build_file = "//ext:dromajo.BUILD",
    patches = ["//ext:dromajo.patch"],
    sha256 = "89c816b17fd5361912c9a3d1f2588cee88c10e61ea0b46254ff7b0500b40ae5d",
    strip_prefix = "dromajo-ff655c3e2ac3dc0c5028105548750d5870fdb441",
    urls = [
      "https://github.com/masc-ucsc/dromajo/archive/ff655c3e2ac3dc0c5028105548750d5870fdb441.zip",
    ],
)

# superbp
http_archive(
    name = "superbp",
    sha256 = "361136f1d8a1c4722ee0f00183b91d849af09438b34a18fa48edf6896298dcde",
    strip_prefix = "superbp-a4a163b00e1ed38e31ae13d865bd1bb97dfea83e",
    urls = [
      "https://github.com/masc-ucsc/superbp/archive/a4a163b00e1ed38e31ae13d865bd1bb97dfea83e.zip",
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

