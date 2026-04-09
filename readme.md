# subprocess

A port of [beman/subprocess](https://github.com/benman64/subprocess) to C++ named modules.

## Dependencies

- CMake 4.2+
- Ninja
- Conan 2

## Build

```bash
conan install . --build=missing --profile=musl -of ./conan --deployer=full_deploy --envs-generation=false
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=/path/to/toolchain.cmake
cmake --build build
cmake --install build
```

## Tests

```bash
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=/path/to/toolchain.cmake -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```
