from conan import ConanFile


class SoemPdoDumpConsumer(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeToolchain", "CMakeDeps"

    def requirements(self):
        self.requires("soem/2.0.0")
        if self.settings.os == "Windows":
            self.requires("npcap/1.70")
