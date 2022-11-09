# Usage  

DESESC runs on Linux and OSX. 

## Debug build

Debug build should be used most of the time with the exception of when you perform large simulations.

```
bazel build -c dbg //...
```

To run the test regression in debug mode:

```
bazel test -c dbg //...
```

## Bazel useful commands

If you update the gcc/clang tools, you may need to do a cleanup in the bazel repo:
```
bazel clean --expunge
```

Sometimes the gcc/clang in your system does not work (too new or bazel still not updated for for the specific version). In that case,
you can specify another compiler. E.g:

```
CXX=g++-12 CXX=gcc-12  bazel build -c dbg //emul:all
```

## Dump memory hierarchy setup

desesc generates a dot file named memory-arch.dot, which is a description of
the memory hierarchy that desesc sees, after parsing desesc.toml.  To view this
in as a png, run the following command. 

```
dot memory-arch.dot -Tpng -o memory-arch.png
```

