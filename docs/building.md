# building

## requirements

 - C++ 20 compatile compiler (tested with gcc-10 and clang-17)
 
 - [boost](https://www.boost.org/) (at least `v1.84.0`)
 - [openssl](https://www.openssl.org/)
 - [protobuf](https://github.com/protocolbuffers/protobuf), (at least `v3.0`)
 - [zlib](https://www.zlib.net/)
 - [rotor](https://github.com/basiliscos/cpp-rotor), (at least `v0.32`)
 - [lz4](https://github.com/lz4/lz4)
 - [libmbdx](https://github.com/erthink/libmdbx), (at least `v0.13.3`)
 - [spdlog](https://github.com/gabime/spdlog)
 - [json](https://github.com/nlohmann/json)
 - [pugixml](https://github.com/zeux/pugixml)
 - [tomlplusplus](https://github.com/marzer/tomlplusplus)
 - [c-ares](https://c-ares.org/)
 - [fltk](https://www.fltk.org/) (for fltk-ui)

The [conan](https://conan.io/) package manager (v2.0+) is used with 
[cmake](https://cmake.org/) build system.

`syncspririt` can be build with [conan](https://conan.io/) or just with bare
[cmake](https://cmake.org/) build system. [conan](https://conan.io/) is 
responsible for installing and building dependecies, whith bare [cmake](https://cmake.org/)
it should be done manually (e.g. use system-provided libraries).

Please note, that [fltk](https://www.fltk.org/) library should be build as 
shared library; otherwise applications will not work correctly.

## generic build 

```
mkdir build.release && cd build.release
conan install --build=missing --output-folder . -s build_type=Release .. 
cmake .. -G "Unix Makefiles" \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/conan_toolchain.cmake \
  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW \
  -DCMAKE_BUILD_TYPE=Release
make -j`nproc`
```

To have locally installed binaries with all dependencies `cmake` command
should be:

```
cmake .. -G "Unix Makefiles" \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/conan_toolchain.cmake \
  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW \
  -DCMAKE_BUILD_TYPE=Release
```

```
make -j`nproc` deploy_syncspirit-fltk
```

For win32 to gater `dll`s it should be

```
make -j`nproc` install deploy_deps
```

## cross building on linux for windows

Generally it the process is the same as above, with the addition, that 
cross-compiler should be installed and conan profiles should be activated

### mingw

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
```

Then make a build

```
mkdir build.release && cd build.release
conan install --build=missing --output-folder . -s build_type=Release \
    --profile:build=default --profile:host=mingw
source ./conanbuild.sh 
cmake .. -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=$PWD/conan_toolchain.cmake \
  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_BUILD_TYPE=Release
make -j`nproc`
```

### mxe

Download [mxe](https://mxe.cc); make sure all requirements are met.

Your `settings.mk` should contain something like:

```
MXE_TARGETS := x86_64-w64-mingw32.shared
```


Validate, that everything is OK via typing in mxe dir something like

```
make zip
```


Add $mxe_dir/uer/bin to your `PATH`, and make sure something like that works:

```
export PATH=`pwd`/usr/bin:$PATH
x86_64-w64-mingw32.shared-g++ --version
x86_64-w64-mingw32.shared-g++ (GCC) 11.2.0
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
CC=x86_64-w64-mingw32.shared-gcc
CXX=x86_64-w64-mingw32.shared-g++
LD=x86_64-w64-mingw32.shared-ld
RC=x86_64-w64-mingw32.shared-windres
```

Go to `syncspirit` dir and then make a build


```
cd syncspirit
mkdir build.release && cd build.release
conan install --build=missing --output-folder . -s build_type=Release \
    --profile:build=default --profile:host=mxe ..
source ./conanbuild.sh 
cmake .. -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=$PWD/conan_toolchain.cmake \
  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_BUILD_TYPE=Release
make -j`nproc`
```

### mxe & windows xp 

Download [mxe](https://mxe.cc); make sure all requirements are met.

Your `settings.mk` should contain something like:

```
MXE_TARGETS := i686-w64-mingw32.shared
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
i686-w64-mingw32.shared-g++ --version
i686-w64-mingw32.shared-g++ (GCC) 11.2.0
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
CC=i686-w64-mingw32.shared-gcc
CXX=i686-w64-mingw32.shared-g++
LD=i686-w64-mingw32.shared-ld
RC=i686-w64-mingw32.shared-windres

[conf]
tools.build:cflags=["-D_WIN32_WINNT=0x0501"]
tools.build:cxxflags=["-D_WIN32_WINNT=0x0501"]
```

The supped libmbdx should be patched for windows xp support:

```
cd syncspirit/lib/mbdx
 patch -p1 < ../mdbx-xp-patch.diff
```

Go to `syncspirit` dir and then make a build

```
cd syncspirit
mkdir build.release && cd build.release
conan install --build=missing --output-folder . -s build_type=Release \
    --profile:build=default --profile:host=xp ..
source ./conanbuild.sh 
cmake .. -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=$PWD/conan_toolchain.cmake \
  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-D_WIN32_WINNT=0x0501 -DBOOST_ASIO_ENABLE_CANCELIO=1"
make -j`nproc`
```


## making appImage (ubuntu 20.04 etc.)

```
debootstrap --arch amd64 focal  ubuntu-root http://archive.ubuntu.com/ubuntu/

mount --bind /proc ubuntu-root/proc/
mount --rbind /dev ubuntu-root/dev/
chroot ubuntu-root


apt update
apt-get install -y software-properties-common
add-apt-repository universe -y
add-apt-repository ppa:ubuntu-toolchain-r/ppa -y
apt update
apt install -y g++-9 gcc-9

apt install -y g++-10 gcc-10
apt-get install -y wget libxft-dev build-essential python3-pip cmake make git fuse libfuse2 libx11-dev libx11-xcb-dev libfontenc-dev libice-dev libsm-dev libxau-dev libxaw7-dev libxcomposite-dev libxcursor-dev libxdamage-dev libxfixes-dev libxi-dev libxinerama-dev libxkbfile-dev libxmuu-dev libxrandr-dev libxrender-dev libxres-dev libxss-dev libxtst-dev libxv-dev libxxf86vm-dev libxcb-glx0-dev libxcb-render0-dev libxcb-render-util0-dev libxcb-xkb-dev libxcb-icccm4-dev libxcb-image0-dev libxcb-keysyms1-dev libxcb-randr0-dev libxcb-shape0-dev libxcb-sync-dev libxcb-xfixes0-dev libxcb-xinerama0-dev libxcb-dri3-dev uuid-dev libxcb-cursor-dev libxcb-dri2-0-dev libxcb-dri3-dev libxcb-present-dev libxcb-composite0-dev libxcb-ewmh-dev libxcb-res0-dev libxcb-util0-dev libxcb-util-dev libglu1-mesa-dev pkgconf

adduser --quiet --disabled-password c

su c
export PATH=$HOME/.local/bin:$PATH
pip3 install --user conan
conan profile detect

cat << EOF > $HOME/.conan2/profiles/default
[settings]
arch=x86_64
build_type=Release
os=Linux
compiler=gcc
compiler.cppstd=gnu17
compiler.libcxx=libstdc++11
compiler.version=10
os=Linux
[buildenv]
CC=gcc-10
CXX=g++-10

[options]
protobuf/*:with_zlib=False
EOF

git clone https://notabug.org/basiliscos/syncspirit.git
cd syncspirit
git checkout v0.4.0-dev
git submodule update --init
mkdir build.chroot
cd build.chroot
conan install --build=missing --output-folder . -s build_type=Release ..
source ./conanbuild.sh
cmake .. -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_BUILD_TYPE=Release
make -j`nproc` deploy_syncspirit-fltk
```
