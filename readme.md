conan install . --build=missing --profile=musl -of ./conan --deployer=full_deploy --envs-generation=false
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=/home/mccakit/dev/toolchains/static/cmake/x86_64-unknown-linux-musl.cmake  && cmake --build build && cmake --install build
