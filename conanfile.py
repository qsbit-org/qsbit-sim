from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake import CMakeDeps, CMakeToolchain


class QsbitDependencies(ConanFile):
    settings = "os", "arch", "compiler", "build_type"
    requires = "systemc/3.0.1"
    default_options = {"systemc/*:shared": False}

    def validate(self):
        if str(self.settings.compiler.cppstd) != "20":
            raise ConanInvalidConfiguration("qsbit-sim requires C++20")
        supported = {"Linux": ("clang", "gcc"), "Macos": ("apple-clang",)}
        if str(self.settings.compiler) not in supported.get(str(self.settings.os), ()):
            raise ConanInvalidConfiguration(
                "Use Clang or GCC on Linux, or Apple Clang on macOS"
            )
        if str(self.settings.build_type) not in ("Debug", "Release"):
            raise ConanInvalidConfiguration("Prepare either Debug or Release dependencies")

    def layout(self):
        compiler = "clang" if str(self.settings.compiler) == "apple-clang" else str(self.settings.compiler)
        configuration = str(self.settings.build_type)
        self.folders.generators = f".conan/{compiler}/{configuration}"
        suffix = "-release" if configuration == "Release" else ""
        self.folders.build = f"build-{compiler}{suffix}"

    def generate(self):
        if str(self.dependencies["systemc"].info.settings.compiler.cppstd) != "20":
            raise ConanInvalidConfiguration(
                "SystemC must use C++20. Apply conan/config before installing dependencies."
            )
        CMakeDeps(self).generate()
        toolchain = CMakeToolchain(self, generator="Ninja")
        toolchain.user_presets_path = False
        toolchain.variables["QSBIT_CONAN_BUILD_TYPE"] = str(self.settings.build_type)
        toolchain.generate()
