# Project
cmake_minimum_required(VERSION 3.4.3)
project(SCAF)

# Programming languages to use
enable_language(C CXX)

# Find and link with LLVM
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")


# By default do not build speculation modules
option(ENABLE_SPECULATION "Enable speculation modules" OFF)

if(ENABLE_SPECULATION)
    # Find and set NOELLE
    if(DEFINED ENV{NOELLE_INSTALL_DIR})
        set(NOELLE_INSTALL_DIR $ENV{NOELLE_INSTALL_DIR})
        message(STATUS "Found NOELLE at ${NOELLE_INSTALL_DIR}")
    else()
        message(FATAL_ERROR "Cannot find NOELLE, did you export $NOELLE_INSTALL_DIR?")
    endif()
endif(ENABLE_SPECULATION)

#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g -Weverything")
#set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -Weverything")
set( CMAKE_EXPORT_COMPILE_COMMANDS ON )

# Link with LLVM
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
#add_definitions(
#-D__STDC_LIMIT_MACROS
#-D__STDC_CONSTANT_MACROS
#)
#link_directories(${LLVM_LIBRARY_DIRS})

# Prepare the pass to be included in the source tree
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

# All include files for SCAF
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include
                    ${CMAKE_CURRENT_BINARY_DIR}/include
                    ${CMAKE_INSTALL_PREFIX}/include
                    ${NOELLE_INSTALL_DIR}/include
                    )

IF(${SVF_AVAILABLE})
  add_definitions(-DSVF_AVAILABLE)
  include_directories(${CMAKE_INSTALL_PREFIX}/include/svf)
  message(STATUS "Including SVF ${CMAKE_INSTALL_PREFIX}/include/svf")
endif()

# Pass
add_subdirectory(lib)

# Install
INSTALL (
 DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ ${CMAKE_CURRENT_BINARY_DIR}/include/ 
DESTINATION include
FILES_MATCHING PATTERN "*.h*")
