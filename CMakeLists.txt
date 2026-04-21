cmake_minimum_required(VERSION 3.20)
project(cps-transpiler)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find LLVM
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVM_DIR: ${LLVM_DIR}")

# Find Clang
find_package(Clang REQUIRED CONFIG)
message(STATUS "Found Clang")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${CLANG_INCLUDE_DIRS})

set(SOURCES
  src/main.cc
  src/recursion_detector.cc
  src/cps_generator.cc
)

add_executable(cps-transpiler ${SOURCES})

# Link against Clang/LLVM libraries
target_link_libraries(cps-transpiler
  clangAST
  clangASTMatchers
  clangBasic
  clangDriver
  clangFrontend
  clangParse
  clangSema
  clangTooling
  LLVMCore
  LLVMSupport
)
