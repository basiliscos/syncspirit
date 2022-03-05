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

