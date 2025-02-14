from conan import ConanFile

class SyncspiritRecipe(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeToolchain", "CMakeDeps"
    options = {
        "shared": [True, False],
    }
    default_options = {
        "shared": True,
        "*/*:shared": True,
        "fltk/*:with_xft": True,
        "fltk/*:with_gl": False,
        "boost/*:magic_autolink": False,
        "boost/*:visibility": "hidden",
        "boost/*:header_only": False,
        "boost/*:without_serialization": True,
        "boost/*:without_graph": True,
        "boost/*:without_fiber": True,
        "boost/*:without_log": True,
        "boost/*:without_math": True,
        "boost/*:without_process": True,
        "boost/*:without_stacktrace": True,
        "boost/*:without_test": True,
        "boost/*:without_wave": True,
        "pugixml/*:no_exceptions": True,
        "rotor/*:enable_asio": True,
        "rotor/*:enable_thread": True,
        "rotor/*:enable_fltk": True,
    }

    def requirements(self):
        self.requires("c-ares/1.28.1")
        self.requires("fltk/1.3.9")
        self.requires("libqrencode/4.1.1")
        self.requires("lz4/1.9.4")
        self.requires("nlohmann_json/3.11.2")
        self.requires("openssl/3.3.2")
        self.requires("protobuf/3.21.12")
        self.requires("pugixml/1.13")
        self.requires("rotor/0.32")
        self.requires("spdlog/1.14.1")
        self.requires("tomlplusplus/3.3.0")
        self.requires("zlib/1.2.13")
        self.requires("catch2/3.3.1")
        self.requires("boost/1.86.0", headers=True, libs=True, transitive_libs=True, force=True)

    def build_requirements(self):
        self.tool_requires("protobuf/3.21.12")
        self.tool_requires("cmake/3.31.5")
