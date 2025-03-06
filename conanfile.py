from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, CMakeDeps, cmake_layout

class SyncspiritRecipe(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
    }

    def requirements(self):
        # self.requires("c-ares/1.28.1")
        self.requires("fltk/1.3.9")
        self.requires("libqrencode/4.1.1")
        self.requires("lz4/1.9.4")
        self.requires("nlohmann_json/3.11.2")
        self.requires("openssl/3.3.2")
        self.requires("protopuf/3.0.0")
        self.requires("pugixml/1.13")
        self.requires("rotor/0.33")
        self.requires("spdlog/1.14.1")
        self.requires("tomlplusplus/3.3.0")
        self.requires("zlib/1.2.13")
        self.requires("catch2/3.3.1")
        self.requires("boost/1.86.0", headers=True, libs=True, transitive_libs=True, force=True)

    def build_requirements(self):
        self.tool_requires("cmake/3.31.5")

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["BUILD_SHARED_LIBS"] = self.options.shared
        tc.generate()
        tc = CMakeDeps(self)
        tc.generate()
