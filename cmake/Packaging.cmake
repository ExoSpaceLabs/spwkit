# SPDX-License-Identifier: Apache-2.0

# Binary distribution metadata. Source installs remain available through the
# normal CMake install/export path; CPack is only an additional release surface.
set(CPACK_PACKAGE_NAME "spwkit")
set(CPACK_PACKAGE_VENDOR "ExoSpaceLabs")
set(CPACK_PACKAGE_CONTACT "ExoSpaceLabs")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_STRIP_FILES ON)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Hosted binary packages are intentionally one self-contained development +
    # runtime/tooling package. The runtime is C11 and the optional C++ layer is
    # header-only, so package compatibility is keyed by architecture/userspace,
    # not by the compiler version used to build it.
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_NAME "spwkit")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "ExoSpaceLabs")
    set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/ExoSpaceLabs/spwkit")
    set(_spwkit_debian_depends "libc6 (>= 2.35)")
    if(SPWKIT_BUILD_CUSE)
        string(APPEND _spwkit_debian_depends ", libfuse3-3")
    endif()
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "${_spwkit_debian_depends}")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_RELEASE "1")

    find_program(SPWKIT_DPKG_EXECUTABLE dpkg)
    if(SPWKIT_DPKG_EXECUTABLE)
        execute_process(
            COMMAND "${SPWKIT_DPKG_EXECUTABLE}" --print-architecture
            OUTPUT_VARIABLE SPWKIT_DEBIAN_ARCHITECTURE
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(SPWKIT_DEBIAN_ARCHITECTURE)
            set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${SPWKIT_DEBIAN_ARCHITECTURE}")
        endif()
    endif()
endif()

include(CPack)
