cmake_minimum_required(VERSION 3.20)

set(LOGO [=[
░█░░░▀█▀░█▀▀░█░█░▀█▀░█▀█░▀█▀░█▀█░█▀▀░
░█░░░░█░░█░█░█▀█░░█░░█░█░░█░░█░█░█░█░
░▀▀▀░▀▀▀░▀▀▀░▀░▀░░▀░░▀░▀░▀▀▀░▀░▀░▀▀▀░
]=])
message(${LOGO})

set(CMAKE_OSX_DEPLOYMENT_TARGET "11" CACHE STRING "Minimum OS X deployment version")

set(CMAKE_CXX_STANDARD 20) # At least C++20 is required

project(pennylane_lightning
    DESCRIPTION "C++ suite of state-vector and tensor network simulators bindings for PennyLane. "
    LANGUAGES CXX
)

# Read and set pennylane_lightning version
function(set_pennylane_lightning_version VERSION_FILE_PATH)
    file(STRINGS ${VERSION_FILE_PATH} VERSION_FILE_STR)
    foreach (LINE IN LISTS VERSION_FILE_STR)
    if("${LINE}" MATCHES "version.*")
        set(VERSION_LINE_STR "${LINE}")
    endif()
    endforeach()

    string(REGEX REPLACE "version = \"(.*)\"" "\\1" VERSION_STRING ${VERSION_LINE_STR})
    set(VERSION_STRING ${VERSION_STRING} PARENT_SCOPE)
endfunction()

set_pennylane_lightning_version(${PROJECT_SOURCE_DIR}/pyproject.toml)

message(STATUS "pennylane_lightning version ${VERSION_STRING}")
set(PROJECT_VERSION ${VERSION_STRING})

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Clang-tidy
option(ENABLE_CLANG_TIDY "Enable clang-tidy build checks" OFF)

# Compile options
option(ENABLE_COVERAGE "Enable code coverage" OFF)
option(ENABLE_WARNINGS "Enable warnings" ON)
option(ENABLE_NATIVE "Enable native CPU build tuning" OFF)
option(ENABLE_PYTHON "Enable compilation of the Python module" ON)

# OpenMP
find_package(OpenMP)
if (OpenMP_CXX_FOUND)
    option(ENABLE_OPENMP "Enable OpenMP" ON)
else()
    option(ENABLE_OPENMP "Enable OpenMP" OFF)
endif()

# Other build options
option(BUILD_TESTS "Build cpp tests" OFF)
option(BUILD_BENCHMARKS "Enable cpp benchmarks" OFF)

# Backend
set(PL_BACKEND "lightning_qubit" CACHE STRING "PennyLane Lightning backend")

# Python bindings are not supported while building multiple backend devices
list(LENGTH PL_BACKEND PL_BACKEND_LEN)
if ((${PL_BACKEND_LEN} GREATER 1) AND ENABLE_PYTHON)
    message(FATAL_ERROR "Lightning does not provide Python support for building multiple backends. Requested backends: ${PL_BACKEND}")
endif()

# Print PL_BACKEND
foreach(BACKEND ${PL_BACKEND})
    message(STATUS "PL_BACKEND: ${BACKEND}")
    if("${BACKEND}" STREQUAL "lightning_tensor")
        set(PL_TENSOR_BACKEND "cutensornet" CACHE STRING "PennyLane LightningTensor backed by cutensornet")
        set(PL_TENSOR "${PL_BACKEND}_${PL_TENSOR_BACKEND}")
    endif()
endforeach()

# Process compile options
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/process_options.cmake")

set(CMAKE_POLICY_DEFAULT_CMP0127 NEW) # To suppress pybind11 CMP0127 warning
# Add pybind11
include(FetchContent)

if(ENABLE_PYTHON)
    find_package(Python COMPONENTS Interpreter Development)
    FetchContent_Declare(pybind11
                        GIT_REPOSITORY https://github.com/pybind/pybind11.git
                        GIT_TAG        v2.13.5
    )
    FetchContent_MakeAvailable(pybind11)
endif()

# Print Python site-packages directory for reference
message("Python site-packages directory: ${Python_SITELIB}")

if(DEFINED PY_INSTALL)
    # Note the following setting is only for pyenv and not for conda
    # TODO: Add support for conda
    if(APPLE)
        set(SCIPY_OPENBLAS32_RUNTIME_LIB_PATH "@loader_path/../scipy_openblas32/lib")
    else()
        set(SCIPY_OPENBLAS32_RUNTIME_LIB_PATH "$ORIGIN/../scipy_openblas32/lib")
    endif()
else()
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindSCIPY_OPENBLAS32.cmake")
    if(NOT DEFINED SCIPY_OPENBLAS32_LIB_PATH AND NOT EXISTS ${SCIPY_OPENBLAS32_LIB_PATH})
        set(SCIPY_OPENBLAS32_LIB_PATH "")
        find_path_to_openblas(SCIPY_OPENBLAS32_LIB_PATH)
        add_compile_definitions(SCIPY_OPENBLAS32_LIB="${SCIPY_OPENBLAS32_LIB_PATH}")
        message(STATUS "SCIPY_OPENBLAS32_LIB_PATH: ${SCIPY_OPENBLAS32_LIB_PATH}")
    else()
        add_compile_definitions(SCIPY_OPENBLAS32_LIB="${SCIPY_OPENBLAS32_LIB_PATH}")
    endif()
    set(SCIPY_OPENBLAS32_RUNTIME_LIB_PATH "${SCIPY_OPENBLAS32_LIB_PATH}")
endif()

set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# All CMakeLists.txt in subdirectories use pennylane_lightning_compile_options and pennylane_lightning_external_libs
add_subdirectory(pennylane_lightning/core)

#####################################################
# Maintain for dependent external package development
#####################################################
add_library(pennylane_lightning INTERFACE)

target_link_libraries(pennylane_lightning INTERFACE lightning_observables
                                                    lightning_utils
                                                    lightning_algorithms
                                                    )

foreach(BACKEND ${PL_BACKEND})
    target_link_libraries(pennylane_lightning INTERFACE "${BACKEND}" #simulator
                                                        "${BACKEND}_algorithms"
                                                        "${BACKEND}_observables"
                                                        "${BACKEND}_bindings"
                                                        "${BACKEND}_measurements"
                                                        )
endforeach()

target_include_directories(pennylane_lightning  INTERFACE "$<INSTALL_INTERFACE:${PROJECT_SOURCE_DIR}/pennylane_lightning/core;include>")

#####################################################
if(ENABLE_PYTHON)
    message(STATUS "ENABLE_PYTHON is ON.")
    pybind11_add_module("${PL_BACKEND}_ops" "pennylane_lightning/core/bindings/Bindings.cpp")

    set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
    if("${PL_BACKEND}" STREQUAL "lightning_gpu" OR "${PL_BACKEND}" STREQUAL "lightning_tensor")
        # Allow pip installation of cuQuantum & CUDA 12 libs to be accessible without setting LD_LIBRARY_PATH for lightning_gpu
        # BUILD_RPATH only works for the last call
        set_target_properties("${PL_BACKEND}_ops" PROPERTIES BUILD_RPATH "$ORIGIN/../cuquantum/lib:$ORIGIN/../nvidia/cuda_runtime/lib:$ORIGIN/../nvidia/nvjitlink/lib:$ORIGIN/../nvidia/cublas/lib:$ORIGIN/../nvidia/cusparse/lib:${SCIPY_OPENBLAS32_RUNTIME_LIB_PATH}:$ORIGIN")
    else()
        set_target_properties("${PL_BACKEND}_ops" PROPERTIES BUILD_RPATH "${SCIPY_OPENBLAS32_RUNTIME_LIB_PATH}")
    endif()

    target_link_libraries("${PL_BACKEND}_ops" PRIVATE   lightning_compile_options
                                                        lightning_external_libs
                                                        )

    target_link_libraries("${PL_BACKEND}_ops" PRIVATE   lightning_observables
                                                        lightning_utils
                                                        lightning_algorithms
                                                        )

    target_link_libraries("${PL_BACKEND}_ops" PRIVATE   ${PL_BACKEND} #simulator
                                                        "${PL_BACKEND}_observables"
                                                        "${PL_BACKEND}_bindings"
                                                        "${PL_BACKEND}_measurements"
                                                        )

    if(NOT DEFINED PL_TENSOR)
        target_link_libraries("${PL_BACKEND}_ops" PRIVATE "${PL_BACKEND}_algorithms")
    endif()

    set_target_properties("${PL_BACKEND}_ops" PROPERTIES CXX_VISIBILITY_PRESET hidden)
    target_compile_definitions("${PL_BACKEND}_ops" PRIVATE VERSION_INFO=${VERSION_STRING})
endif()

install(TARGETS pennylane_lightning
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        INCLUDES DESTINATION include
        PUBLIC_HEADER DESTINATION include
)

install(DIRECTORY
    ${PROJECT_SOURCE_DIR}/pennylane_lightning/core
    DESTINATION include/pennylane_lightning/core/
)

if (BUILD_TESTS)
    enable_testing()
endif()
