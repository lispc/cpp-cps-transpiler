cmake_minimum_required(VERSION 3.20)
project(cps-transpiler VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Find LLVM/Clang
# ---------------------------------------------------------------------------
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVM_DIR: ${LLVM_DIR}")

find_package(Clang REQUIRED CONFIG)
message(STATUS "Clang_DIR: ${Clang_DIR}")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

# ---------------------------------------------------------------------------
# Main transpiler executable
# ---------------------------------------------------------------------------
set(SOURCES
  src/main.cc
  src/cps_generator.cc
  src/transformation_rules.cc
  src/transformation_rules_helpers.cc
  src/transformation_rule_tail.cc
  src/transformation_rule_acc.cc
  src/transformation_rule_tupling.cc
  src/transformation_rule_memo.cc
  src/transformation_rule_binary.cc
  src/transformation_rule_generic.cc
  src/transformation_rule_defun.cc
  src/transformation_rule_tree.cc
  src/transformation_rule_structural_subrules.cc
  src/transformation_rule_string.cc
)

add_executable(cps-transpiler ${SOURCES})

target_include_directories(cps-transpiler
  PRIVATE
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)

# LLVM_DEFINITIONS may contain -D flags; split them for target_compile_definitions.
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
target_compile_definitions(cps-transpiler PRIVATE ${LLVM_DEFINITIONS_LIST})

target_link_libraries(cps-transpiler
  PRIVATE
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

# ---------------------------------------------------------------------------
# Testing
# ---------------------------------------------------------------------------
enable_testing()

add_custom_target(check
  COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/run_tests.py
  DEPENDS cps-transpiler
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Running CPS transpiler test suite"
  USES_TERMINAL
)

add_test(NAME run-tests
  COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/run_tests.py
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
set_tests_properties(run-tests PROPERTIES DEPENDS cps-transpiler)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS cps-transpiler
  RUNTIME DESTINATION bin
)
