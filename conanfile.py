from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, CMakeDeps, cmake_layout

class SyncspiritRecipe(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "frontend_fltk": [True, False],
    }
    default_options = {
        "shared": True,
        "frontend_fltk": True,
    }

    def requirements(self):
        rotor_options = {"enable_asio": True, "enable_thread": True}

        if self.options.get_safe("frontend_fltk", False):
            self.requires("freetype/2.12.1", headers=True, libs=True, transitive_libs=True, force=True)
            self.requires("fltk/1.4.5")
            self.requires("libqrencode/4.1.1")
            rotor_options["enable_fltk"] = True

        self.requires("rotor/0.40", options=rotor_options)
        self.requires("lz4/1.10.0")
        self.requires("nlohmann_json/3.12.0")
        self.requires("openssl/3.5.2")
        self.requires("protopuf/3.0.0")
        self.requires("pugixml/1.15")
        self.requires("spdlog/1.15.3")
        self.requires("tomlplusplus/3.4.0")
        self.requires("zlib/1.3.1")
#        self.requires("c-ares/1.34.5")
        self.requires("catch2/3.3.1")
#        self.requires("uni-algo/1.2.0")
        self.requires("boost/1.86.0", headers=True, libs=True, transitive_libs=True, force=True)

    def build_requirements(self):
        self.tool_requires("cmake/3.31.5")

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["BUILD_SHARED_LIBS"] = self.options.shared
        tc.variables["SYNCSPIRIT_FRONTEND_FLTK"] = self.options.get_safe("frontend_fltk", False)
        tc.generate()
        tc = CMakeDeps(self)
        tc.generate()
