# building

## requirements

 - C++ 17 compatile compiler (tested with gcc-10 and clang-17)
 - [boost](https://www.boost.org/) (at least `v1.70`)
 - [openssl](https://www.openssl.org/)
 - [protobuf](https://github.com/protocolbuffers/protobuf), (at least `v3.0`)
 - [zlib](https://www.zlib.net/)

## shipped dependencies (bundle as subrepos)

 - [rotor](https://github.com/basiliscos/cpp-rotor)
 - [libmbdx](https://github.com/erthink/libmdbx)
 - [spdlog](https://github.com/gabime/spdlog)
 - [lz4](https://github.com/lz4/lz4)
 - [json](https://github.com/nlohmann/json)
 - [pugixml](https://github.com/zeux/pugixml)
 - [tomlplusplus](https://github.com/marzer/tomlplusplus)
 - [uriparser](https://github.com/uriparser/uriparser)

# building (linux)

```
git clone https://github.com/basiliscos/syncspirit.git syncspirit
cd syncspirit
git submodule update --init
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
ctest .
```

Tested on [voidlinux](https://voidlinux.org/) and
[termux](https://termux.com/) (it means `syncspirit` and run
on mobile devices in linux terminal)

# cross building for windows

Install [mxe](https://mxe.cc/), and then build syncspirit requirements:

```
make boost openssl zlib cmake protobuf
```

Then build `syncspirit` as usual

```
git clone https://github.com/basiliscos/syncspirit.git syncspirit
cd syncspirit
git submodule update --init
mkdir build
cd build
x86_64-w64-mingw32.static-cmake -DCMAKE_BUILD_TYPE=Release ..
x86_64-w64-mingw32.static-cmake --build .
x86_64-w64-mingw32.static-strip src/ui-daemon/syncspirit-daemon.exe
```


# cross building for ubuntu

```
7z x /tmp/boost_1_78_0.7z
cd boost_1_78_0
./bootstrap.sh
echo 'using gcc : ubuntu : "x86_64-ubuntu14.04-linux-gnu-g++" : ;' >> project-config.jam
./tools/build/src/engine/bjam install --prefix=/home/b/development/cpp/syncspirit-cross/sysroot -j 8 toolset=gcc-ubuntu cxxstd=17 variant=release link=static local-visibility=hidden visibility=hidden optimization=space runtime-link=static threading=multi

tar -xf /tmp/zlib-1.2.11.tar.xz
cd zlib-1.2.11
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=/home/b/development/cpp/syncspirit/misc/ubuntu14.04.toolchain -DCMAKE_INSTALL_PREFIX:PATH=/home/b/development/cpp/syncspirit-cross/sysroot ..
make -j8 all install
rm /home/b/development/cpp/syncspirit-cross/sysroot/lib/libz.s*

export PATH=/home/b/x-tools/x86_64-ubuntu14.04-linux-gnu/bin/:$PATH

tar -xf /tmp/openssl-openssl-3.0.1.tar.gz
cd openssl-openssl-3.0.1
./Configure --prefix=/home/b/development/cpp/syncspirit-cross/sysroot zlib no-shared -static linux-generic64 --with-zlib-include=/home/b/development/cpp/syncspirit-cross/sysroot/include --with-zlib-lib=/home/b/development/cpp/syncspirit-cross/sysroot/lib
make -j9 CC=x86_64-ubuntu14.04-linux-gnu-gcc RANLIB=x86_64-ubuntu14.04-linux-gnu-ranlib LD=x86_64-ubuntu14.04-linux-gnu-ld MAKEDEPPROG=x86_64-ubuntu14.04-linux-gnu-gcc
make install

tar -xf /tmp/protobuf-cpp-3.19.4.tar.gz
cd protobuf-3.19.4
export CC=x86_64-ubuntu14.04-linux-gnu-gcc
export CXX=x86_64-ubuntu14.04-linux-gnu-g++
./configure --host=x86_64-ubuntu14.04-linux-gnu --build=x86_64-linux-gnu  --disable-shared --with-sysroot=/home/b/development/cpp/syncspirit-cross/sysroot --prefix=/home/b/development/cpp/syncspirit-cross/sysroot --with-zlib-include=/home/b/development/cpp/syncspirit-cross/sysroot/include --with-zlib-lib=/home/b/development/cpp/syncspirit-cross/sysroot/lib
make -j8 && make install

export PATH=/home/b/development/cpp/syncspirit-cross/sysroot/bin:$PATH
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=/home/b/development/cpp/syncspirit/misc/ubuntu14.04.toolchain  -DBoost_USE_STATIC_RUNTIME=on ..
```

# termux

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
