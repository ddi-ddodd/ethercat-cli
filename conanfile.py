from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMakeDeps, CMake, cmake_layout
from conan.tools.files import get


class SoemConan(ConanFile):
    name = "soem"
    version = "2.0.0"
    description = "Simple Open EtherCAT Master library"
    license = "GPL-3.0"
    homepage = "https://github.com/OpenEtherCATsociety/SOEM"
    topics = ("ethercat", "fieldbus", "industrial")

    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}

    def requirements(self):
        if self.settings.os == "Windows":
            self.requires("npcap/1.70")

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        # SOEM is pure C; strip C++ ABI settings to avoid hash mismatches
        self.settings.rm_safe("compiler.libcxx")
        self.settings.rm_safe("compiler.cppstd")

    def layout(self):
        # cmake_layout separates source_folder from build_folder in the Conan
        # cache. This ensures CMakeToolchain writes conan_toolchain.cmake and
        # CMakePresets.json into the *build* folder, not the *source* folder
        # where SOEM's own CMakePresets.json already lives (which Conan would
        # refuse to overwrite).
        cmake_layout(self)

    def source(self):
        get(
            self,
            "https://github.com/OpenEtherCATsociety/SOEM/archive/refs/tags/v2.0.0.zip",
            strip_root=True,
        )

    def generate(self):
        # Generators write into self.build_folder (separate from source),
        # so no collision with SOEM's CMakePresets.json.
        tc = CMakeToolchain(self)
        # Disable sample builds: eni_test requires eniconv.py which is not
        # available in the build environment (and we only need the library).
        tc.cache_variables["SOEM_BUILD_SAMPLES"] = False
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["soem"]
        if self.settings.os == "Linux":
            self.cpp_info.system_libs = ["pthread", "rt"]
        elif self.settings.os == "Windows":
            # winmm: timeBeginPeriod/timeEndPeriod used by osal.c
            # ws2_32: htons/ntohs/Winsock used by nicdrv.c and oshw.c
            self.cpp_info.system_libs = ["winmm", "ws2_32"]
        # Tell CMakeDeps what find_package name and target name to generate
        self.cpp_info.set_property("cmake_file_name", "soem")
        self.cpp_info.set_property("cmake_target_name", "soem")
