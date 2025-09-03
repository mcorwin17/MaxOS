# =============================================================================
# MaxOS - Educational Operating System - CMake Build System
# =============================================================================
# 
# This CMake configuration provides a modern, cross-platform build system
# for MaxOS with multiple build types and comprehensive testing support.
#
# @author Maxwell Corwin
# @date 2025
# @version 2.0
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(MaxOS VERSION 2.0.0 LANGUAGES C ASM)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Project metadata
set(PROJECT_DESCRIPTION "A functional x86 operating system developed from scratch for educational purposes")
set(PROJECT_HOMEPAGE_URL "https://github.com/mcorwin17/MaxOS")
set(PROJECT_LICENSE "MIT")

# Build options
option(BUILD_TESTS "Build test suite" OFF)
option(BUILD_DOCS "Build documentation" OFF)
option(BUILD_FLOPPY_IMAGE "Build bootable floppy disk image" ON)
option(ENABLE_QEMU_TESTING "Enable QEMU testing support" ON)

# Compiler-specific flags
if(MSVC)
    add_compile_options(/W4 /WX)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-fdiagnostics-color=always)
    endif()
endif()

# Assembly-specific flags
if(CMAKE_ASM_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -f bin")
endif()

# Optimization flags
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -DNDEBUG")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -O0 -DDEBUG")

# Find required packages
find_package(Threads REQUIRED)

# Optional packages
if(ENABLE_QEMU_TESTING)
    find_program(QEMU_SYSTEM_386 qemu-system-i386)
    if(QEMU_SYSTEM_386)
        message(STATUS "QEMU found: ${QEMU_SYSTEM_386}")
    else()
        message(WARNING "QEMU not found - testing will be disabled")
        set(ENABLE_QEMU_TESTING OFF)
    endif()
endif()

# Source files
set(BOOTLOADER_SOURCES
    bootloader/boot.asm
    bootloader/gdt.asm
    bootloader/switchpm.asm
    bootloader/ppstring.asm
    bootloader/pstring.asm
    bootloader/println.asm
    bootloader/load_kernel.asm
)

set(KERNEL_SOURCES
    kernel/kernel.c
    kernel/link.ld
)

set(HEADERS
    kernel/kernel.c
)

# Create bootloader binary
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/boot.bin
    COMMAND nasm -f bin ${CMAKE_SOURCE_DIR}/bootloader/boot.asm -o ${CMAKE_BINARY_DIR}/boot.bin
    DEPENDS ${BOOTLOADER_SOURCES}
    COMMENT "Assembling bootloader"
    VERBATIM
)

# Create kernel binary
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/kernel.bin
    COMMAND ${CMAKE_C_COMPILER} -ffreestanding -c ${CMAKE_SOURCE_DIR}/kernel/kernel.c -o ${CMAKE_BINARY_DIR}/kernel.o
    COMMAND cp ${CMAKE_BINARY_DIR}/kernel.o ${CMAKE_BINARY_DIR}/kernel.bin
    DEPENDS ${KERNEL_SOURCES}
    COMMENT "Compiling kernel"
    VERBATIM
)

# Create floppy disk image
if(BUILD_FLOPPY_IMAGE)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/maxos.img
        COMMAND dd if=/dev/zero of=${CMAKE_BINARY_DIR}/maxos.img bs=512 count=2880
        COMMAND dd if=${CMAKE_BINARY_DIR}/boot.bin of=${CMAKE_BINARY_DIR}/maxos.img conv=notrunc bs=512 count=1
        COMMAND dd if=${CMAKE_BINARY_DIR}/kernel.bin of=${CMAKE_BINARY_DIR}/maxos.img conv=notrunc bs=512 seek=1
        DEPENDS ${CMAKE_BINARY_DIR}/boot.bin ${CMAKE_BINARY_DIR}/kernel.bin
        COMMENT "Creating floppy disk image"
        VERBATIM
    )
    
    add_custom_target(floppy_image ALL DEPENDS ${CMAKE_BINARY_DIR}/maxos.img)
endif()

# QEMU testing target
if(ENABLE_QEMU_TESTING AND QEMU_SYSTEM_386)
    add_custom_target(qemu_test
        COMMAND ${QEMU_SYSTEM_386} -fda ${CMAKE_BINARY_DIR}/maxos.img -boot a -nographic
        DEPENDS ${CMAKE_BINARY_DIR}/maxos.img
        COMMENT "Testing MaxOS in QEMU"
        VERBATIM
    )
    
    add_custom_target(qemu_debug
        COMMAND ${QEMU_SYSTEM_386} -fda ${CMAKE_BINARY_DIR}/maxos.img -boot a -nographic -s -S
        DEPENDS ${CMAKE_BINARY_DIR}/maxos.img
        COMMENT "Starting MaxOS in QEMU with debug support"
        VERBATIM
    )
endif()

# Testing
if(BUILD_TESTS)
    enable_testing()
    
    # Add test executable
    add_executable(${PROJECT_NAME}_tests
        tests/test_main.c
        tests/test_bootloader.c
        tests/test_kernel.c
    )
    
    # Add tests
    add_test(NAME BootloaderTests COMMAND ${PROJECT_NAME}_tests --gtest_filter="Bootloader*")
    add_test(NAME KernelTests COMMAND ${PROJECT_NAME}_tests --gtest_filter="Kernel*")
endif()

# Documentation
if(BUILD_DOCS)
    find_package(Doxygen)
    if(DOXYGEN_FOUND)
        set(DOXYGEN_IN ${CMAKE_SOURCE_DIR}/docs/Doxyfile.in)
        set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
        
        configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
        
        add_custom_target(docs ALL
            COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Generating API documentation with Doxygen"
            VERBATIM
        )
    endif()
endif()

# Package configuration
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

# Export targets
export(EXPORT ${PROJECT_NAME}Targets
    FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Targets.cmake"
    NAMESPACE ${PROJECT_NAME}::
)

# CPack configuration
set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
set(CPACK_PACKAGE_VENDOR "Maxwell Corwin")
set(CPACK_PACKAGE_CONTACT "maxwellcorwin13@gmail.com")
set(CPACK_PACKAGE_HOMEPAGE_URL ${PROJECT_HOMEPAGE_URL})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop;TGZ")
else()
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
endif()

include(CPack)

# Print configuration summary
message(STATUS "")
message(STATUS "MaxOS Configuration Summary")
message(STATUS "==========================")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C standard: ${CMAKE_C_STANDARD}")
message(STATUS "Build tests: ${BUILD_TESTS}")
message(STATUS "Build docs: ${BUILD_DOCS}")
message(STATUS "Build floppy image: ${BUILD_FLOPPY_IMAGE}")
message(STATUS "QEMU testing: ${ENABLE_QEMU_TESTING}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")

# Custom targets for common operations
add_custom_target(clean_all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMENT "Cleaning all build artifacts"
)

add_custom_target(rebuild
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target clean
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
    COMMENT "Rebuilding project"
)

# Help target
add_custom_target(help
    COMMAND ${CMAKE_COMMAND} --help
    COMMENT "Showing CMake help"
)
