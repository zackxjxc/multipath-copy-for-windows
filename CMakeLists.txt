cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" MULTIPATH_COPY_VERSION)
string(STRIP "${MULTIPATH_COPY_VERSION}" MULTIPATH_COPY_VERSION)
if(NOT MULTIPATH_COPY_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must use the MAJOR.MINOR.PATCH format")
endif()

project(MultiPathCopy VERSION "${MULTIPATH_COPY_VERSION}" LANGUAGES CXX RC)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
    src/version.rc.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc"
    @ONLY
)

add_executable(
    MultiPathCopy
    WIN32
    src/main.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc"
)
target_compile_features(MultiPathCopy PRIVATE cxx_std_17)
target_link_libraries(MultiPathCopy PRIVATE advapi32 shell32 shlwapi user32)

install(TARGETS MultiPathCopy RUNTIME DESTINATION .)

if(MSVC)
    target_compile_options(MultiPathCopy PRIVATE /utf-8 /W4 /permissive-)
else()
    target_compile_options(MultiPathCopy PRIVATE -Wall -Wextra -Wpedantic)
endif()
