# Shadowdash

Shadowdash is a lightweight build system based on Ninja, focused on increasing build speed.

## CMake
1. Build Shadowdash and move ninja executable file to your local/bin:
```bash
mkdir build && cd build
cmake -S ..
make -j $(nproc)
sudo cp ninja /usr/local/bin/
ninja --version
```
2. Build Clang-Tidy from source code:
```bash
git clone https://github.com/llvm/llvm-project.git
cd llvm-project
mkdir build
cd build
cmake -G "Ninja" -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra" -DCMAKE_BUILD_TYPE=Release ../llvm
ninja clang-tidy
export PATH=$PATH:/path/to/your/llvm-project/build/bin
clang-tidy --version
```
Alternative Installation via Package Manager (Debian-based Systems):
If you’re using a Debian-based distribution like Ubuntu, you can install Clang-Tidy using the following command:
```bash
sudo apt-get install -y clang-tidy
```
3. Test:
```bash
cd build
ctest
```

The `ninja` binary will now be inside the `build-cmake` directory (you can
choose any other name you like).

To run the unit tests:
```
./build-cmake/ninja_test
```
