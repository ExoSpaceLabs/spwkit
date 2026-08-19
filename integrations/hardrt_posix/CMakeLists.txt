cmake_minimum_required(VERSION 3.20)
project(spwkit_hardrt_posix_integration LANGUAGES C)

find_package(HardRT 0.4 CONFIG REQUIRED)
find_package(SpWKit 0.5 CONFIG REQUIRED)

if(NOT TARGET HardRT::hardrt)
    message(FATAL_ERROR "Installed HardRT package does not export HardRT::hardrt")
endif()
if(NOT TARGET spwkit::spwkit)
    message(FATAL_ERROR "Installed SpWKit package does not export spwkit::spwkit")
endif()

add_executable(spwkit_hardrt_posix main.c)
target_link_libraries(spwkit_hardrt_posix PRIVATE HardRT::hardrt spwkit::spwkit)
set_target_properties(spwkit_hardrt_posix PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS OFF
)
