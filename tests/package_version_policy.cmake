# SPDX-License-Identifier: Apache-2.0
if(NOT DEFINED VERSION_FILE OR NOT EXISTS "${VERSION_FILE}")
    message(FATAL_ERROR "VERSION_FILE is required")
endif()

function(check_version major minor patch expected_compatible expected_exact)
    set(PACKAGE_FIND_VERSION "${major}.${minor}.${patch}")
    set(PACKAGE_FIND_VERSION_MAJOR "${major}")
    set(PACKAGE_FIND_VERSION_MINOR "${minor}")
    set(PACKAGE_FIND_VERSION_PATCH "${patch}")
    set(PACKAGE_FIND_VERSION_TWEAK 0)
    set(PACKAGE_FIND_VERSION_COUNT 3)
    unset(PACKAGE_VERSION)
    unset(PACKAGE_VERSION_COMPATIBLE)
    unset(PACKAGE_VERSION_EXACT)
    unset(PACKAGE_VERSION_UNSUITABLE)

    include("${VERSION_FILE}")

    if(expected_compatible)
        if(NOT PACKAGE_VERSION_COMPATIBLE)
            message(FATAL_ERROR
                "expected ${PACKAGE_FIND_VERSION} to be compatible with ${PACKAGE_VERSION}")
        endif()
    elseif(PACKAGE_VERSION_COMPATIBLE)
        message(FATAL_ERROR
            "expected ${PACKAGE_FIND_VERSION} to be incompatible with ${PACKAGE_VERSION}")
    endif()

    if(expected_exact)
        if(NOT PACKAGE_VERSION_EXACT)
            message(FATAL_ERROR "expected ${PACKAGE_FIND_VERSION} to be exact")
        endif()
    elseif(PACKAGE_VERSION_EXACT)
        message(FATAL_ERROR "did not expect ${PACKAGE_FIND_VERSION} to be exact")
    endif()
endfunction()

check_version("${CURRENT_MAJOR}" "${CURRENT_MINOR}" "${CURRENT_PATCH}" TRUE TRUE)

math(EXPR next_minor "${CURRENT_MINOR} + 1")
check_version("${CURRENT_MAJOR}" "${next_minor}" 0 FALSE FALSE)

if(CURRENT_MAJOR EQUAL 0 AND CURRENT_MINOR GREATER 0)
    math(EXPR previous_minor "${CURRENT_MINOR} - 1")
    check_version(0 "${previous_minor}" 0 FALSE FALSE)
elseif(CURRENT_MAJOR GREATER 0 AND CURRENT_MINOR GREATER 0)
    math(EXPR previous_minor "${CURRENT_MINOR} - 1")
    check_version("${CURRENT_MAJOR}" "${previous_minor}" 0 TRUE FALSE)
endif()
