# coding: utf-8
#
#   .__  ._____.
#   |  | |__\_ |__
#   |  | |  || __ \
#   |  |_|  || \_\ \     .__
#   |____/__||___  /____ |__| _____   ____
#                \/     \|  |/     \_/ __ \
#                |  Y Y  \  |  Y Y  \  ___/
#                |__|_|  /__|__|_|  /\___  >
#                      \/         \/     \/
#
#   libmime
#   A C++ library for inferring MIME content-types from pathnames
#
#   Copyright © 2018 D.E. Goodman-Wilson
#

from conans import ConanFile, CMake, tools
import os

class MimetypesTestConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake"

    def build(self):
        cmake = CMake(self)
        # Current dir is "test_package/build/<build_id>" and CMakeLists.txt is in "test_package"
        cmake.configure()
        cmake.build()

    def imports(self):
        self.copy("*.dll", dst="bin", src="bin")
        self.copy("*.dylib*", dst="bin", src="lib")
        self.copy('*.so*', dst='bin', src='lib')

    def test(self):
        if not tools.cross_building(self.settings):
            os.chdir("bin")
            self.run(".%sexample" % os.sep)
