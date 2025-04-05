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
    sha256 = "25af8ca914f0ca3f41a6250fa6069621e3fcaa3230e1bc95ff813eaec9c7e3dd",
    strip_prefix = "dromajo-2a9f5bbfbefa92bc0d81bb83e44e6bab963302cb",
    urls = [
      "https://github.com/masc-ucsc/dromajo/archive/2a9f5bbfbefa92bc0d81bb83e44e6bab963302cb.zip",
    ],
)

# superbp
http_archive(
    name = "superbp",
    sha256 = "3d15f931c7aaf3914cf4d4e7f8588fff7be3568fc3a5df6aa07deeb5769b7d34",
    strip_prefix = "superbp-d81de6bf1409f2320c1242e8e4b711334e92d1b4",
    urls = [
      "https://github.com/masc-ucsc/superbp/archive/d81de6bf1409f2320c1242e8e4b711334e92d1b4.zip",
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

