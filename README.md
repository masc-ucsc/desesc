# desesc
Dromajo ESESC

## Setup

Install [bazelisk](https://github.com/bazelbuild/bazelisk) in the system (allows to pick a specific bazel version). Notice
that if you have bazel, it should match the required version.

To build `desesc` with debug version (debug/develop)
```
bazel build -c dbg //main:desesc
```

To build `desesc` with release version (long simulations)
```
bazel build -c dbg //main:desesc
```

To run the default simulation (dhrystone):
```
./bazel-bin/main/desesc -c ./conf/desesc.toml
```
