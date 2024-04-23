# building

## requirements

 - C++ 17 compatile compiler (tested with gcc-8.5 and clang-17)
 
 - [boost](https://www.boost.org/) (at least `v1.70`)
 - [openssl](https://www.openssl.org/)
 - [protobuf](https://github.com/protocolbuffers/protobuf), (at least `v3.0`)
 - [zlib](https://www.zlib.net/)
 - [rotor](https://github.com/basiliscos/cpp-rotor)
 - [lz4](https://github.com/lz4/lz4)
 - [libmbdx](https://github.com/erthink/libmdbx)
 - [spdlog](https://github.com/gabime/spdlog)
 - [json](https://github.com/nlohmann/json)
 - [pugixml](https://github.com/zeux/pugixml)
 - [tomlplusplus](https://github.com/marzer/tomlplusplus)
 - [uriparser](https://github.com/uriparser/uriparser)
 
The [conan](https://conan.io/) package manager (v2.0+) is used with 
[cmake](https://cmake.org/) build system.

## building linux (shared libraries)

```
mkdir build.release && cd build.release
conan install --build=missing -o '*:shared=True' -o shared=True --output-folder . -s build_type=Release .. 
cmake .. -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=$PWD/conan_toolchain.cmake \
  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=on
make -j`nproc`
```

## building linux (static libraries, single executable)

```
mkdir build.release && cd build.release
conan install --build=missing -o '*:shared=false' -o shared=False --output-folder . -s build_type=Release .. 
cmake .. -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=$PWD/conan_toolchain.cmake \
  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=off
make -j`nproc`
```


## cross building on linux for windows (single executable, simple way) 

Install mingw on linux (something like `cross-x86_64-w64-mingw32`)

Make a conan profile for mingw:

```
cat ~/.conan2/profiles/mingw 
[settings]
os=Windows
arch=x86_64
compiler=gcc
build_type=Release
compiler.cppstd=gnu17
compiler.libcxx=libstdc++11
compiler.version=12
[buildenv]
CC=x86_64-w64-mingw32-gcc
CXX=x86_64-w64-mingw32-g++
LD=ix86_64-w64-mingw32-ld
RC=x86_64-w64-mingw32-windres

[options]
boost/*:without_fiber=True
boost/*:without_graph=True
boost/*:without_log=True
boost/*:without_stacktrace=True
boost/*:without_test=True
boost/*:without_wave=True
```

Then make a build

```
mkdir build.release && cd build.release
conan install --build=missing -o '*:shared=False' -o shared=False --output-folder . \
    -s build_type=Release --profile:build=default --profile:host=mingw
source ./conanbuild.sh 
cmake .. -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=$PWD/conan_toolchain.cmake \
  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=off
make -j`nproc`
```

## cross building on linux for modern windows (single executable, more reliable way) 

Download [mxe](https://mxe.cc); make sure all requirements are met.

Your `settings.mk` should contain something like:

```
MXE_TARGETS := x86_64-w64-mingw32.static
```


Validate, that everything is OK via typing in mxe dir something like

```
make zip
```


Add $mxe_dir/uer/bin to your `PATH`, and make sure something like that works:

```
export PATH=`pwd`/usr/bin:$PATH
x86_64-w64-mingw32.static-g++ --version
x86_64-w64-mingw32.static-g++ (GCC) 11.2.0
```

Make a conan profile for mingw:

```
cat ~/.conan2/profiles/mxe
[settings]
os=Windows
arch=x86_64
compiler=gcc
build_type=Release
compiler.cppstd=gnu17
compiler.libcxx=libstdc++11
compiler.version=11
[buildenv]
CC=x86_64-w64-mingw32.static-gcc
CXX=x86_64-w64-mingw32.static-g++
LD=x86_64-w64-mingw32.static-ld
RC=x86_64-w64-mingw32.static-windres

[options]
boost/*:without_fiber=True
boost/*:without_graph=True
boost/*:without_log=True
boost/*:without_stacktrace=True
boost/*:without_test=True
boost/*:without_wave=True
```

Go to `syncspirit` dir and then make a build


```
cd syncspirit
mkdir build.release && cd build.release
conan install --build=missing -o '*:shared=False' -o shared=False --output-folder . \
    -s build_type=Release --profile:build=default --profile:host=mxe ..
source ./conanbuild.sh 
cmake .. -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=$PWD/conan_toolchain.cmake \
  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=off
make -j`nproc`
```

## cross building on linux for windows xp 

Download [mxe](https://mxe.cc); make sure all requirements are met.

Your `settings.mk` should contain something like:

```
MXE_TARGETS := i686-w64-mingw32.static
MXE_PLUGIN_DIRS=plugins/windows-xp
```


Validate, that everything is OK via typing in mxe dir something like

```
make zip
```

Copy the resulting `zip.exe` to windows xp host and launch, i.e. make sure
everything is ok with the toolchain.


Add $mxe_dir/uer/bin to your `PATH`, and make sure something like that works:

```
export PATH=`pwd`/usr/bin:$PATH
i686-w64-mingw32.static-g++ --version
i686-w64-mingw32.static-g++ (GCC) 11.2.0
```

Make a conan profile for mingw:

```
cat ~/.conan2/profiles/xp
[settings]
os=Windows
arch=x86
compiler=gcc
build_type=Release
compiler.cppstd=gnu17
compiler.libcxx=libstdc++11
compiler.version=12
[buildenv]
CC=i686-w64-mingw32.static-gcc
CXX=i686-w64-mingw32.static-g++
LD=i686-w64-mingw32.static-ld
RC=i686-w64-mingw32.static-windres

[options]
boost/*:without_fiber=True
boost/*:without_graph=True
boost/*:without_log=True
boost/*:without_stacktrace=True
boost/*:without_test=True
boost/*:without_wave=True

[conf]
tools.build:cflags=["-D_WIN32_WINNT=0x0501"]
tools.build:cxxflags=["-D_WIN32_WINNT=0x0501"]
```

The supped libmbdx should be patched for windows xp support:

```
cd syncspirit/lib/mbdx
 patch -p1 < ../windows-xp.patch
```

Go to `syncspirit` dir and then make a build

```
cd syncspirit
mkdir build.release && cd build.release
conan install --build=missing -o '*:shared=False' -o shared=False --output-folder . \
    -s build_type=Release --profile:build=default --profile:host=xp ..
source ./conanbuild.sh 
cmake .. -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=$PWD/conan_toolchain.cmake \
  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=off \
  -DCMAKE_CXX_FLAGS="-D_WIN32_WINNT=0x0501 -DBOOST_ASIO_ENABLE_CANCELIO=1"
make -j`nproc`
```


## cross building on linux for old linux (ubuntu 16.04 etc.)

```
git clone https://github.com/crosstool-ng/crosstool-ng.git
cd crosstool
git checkout crosstool-ng-1.26.0
./bootstrap
./configure --enable-local
make -j`nproc`
./ct-ng menuconfig
```

select linux, some gcc comiler with **static build** and an olders supported
glibc.

```
./ct-ng build
export PATH=/home/b/x-tools/x86_64-syncspirit-linux-gnu/bin:$PATH
cat ~/.conan2/profiles/old_linux 
[settings]
os=Linux
arch=x86_64
compiler=gcc
build_type=Release
compiler.cppstd=gnu17
compiler.libcxx=libstdc++11
compiler.version=8

[buildenv]
CC=x86_64-syncspirit-linux-gnu-gcc
CXX=x86_64-syncspirit-linux-gnu-g++
LD=x86_64-syncspirit-linux-gnu-ld

[options]
boost/*:without_fiber=True
boost/*:without_graph=True
boost/*:without_log=True
boost/*:without_stacktrace=True
boost/*:without_test=True
boost/*:without_wave=True
```

```
conan install --build=missing -o '*:shared=False' -o shared=False --output-folder . 
    -s build_type=Release --profile:build=default --profile:host=old_linux ..
source ./conanbuild.sh 
cmake .. -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=$PWD/conan_toolchain.cmake \
  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=off
make -j`nproc`

```

## termux

```
pkg install git cmake boost-headers protobuf
git clone https://github.com/basiliscos/syncspirit.git 
cd syncspirit
git checkout v0.2.0
git submodule update --init
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j1 (or -j2 or -j3)
```
