cmake_minimum_required(VERSION 3.20)
project(soem-pdo-dump VERSION 1.0.0 LANGUAGES CXX)

# Conan-generated toolchain sets CMAKE_PREFIX_PATH so find_package resolves
find_package(soem REQUIRED)

add_executable(soem-pdo-dump
    src/main.cpp
    src/pdo.cpp
    src/ethercat.cpp
)

target_link_libraries(soem-pdo-dump PRIVATE soem)

# pthread and rt propagate automatically from SOEM's exported CMake target
target_compile_features(soem-pdo-dump PRIVATE cxx_std_17)

# On Windows, pcap.h (included first via soem/nicdrv.h) defines BPF_MAJOR_VERSION
# and the bpf_program/bpf_insn structs. Packet32.h guards its own copies with
# #ifndef BPF_MAJOR_VERSION, but only if the macro is already defined when
# Packet32.h is parsed. Pre-defining it here suppresses the redefinition errors.
if(WIN32)
    target_compile_definitions(soem-pdo-dump PRIVATE BPF_MAJOR_VERSION=1)
endif()

message(STATUS "NOTE: soem-pdo-dump requires root or CAP_NET_RAW to open a raw socket")
message(STATUS "Usage: sudo ./soem-pdo-dump <interface>   e.g.: sudo ./soem-pdo-dump eth0")
