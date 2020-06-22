SC_BEGIN_PACKAGE(GA)

SET(Boost_DETAILED_FAILURE_MSG ON)
FIND_PACKAGE(Boost REQUIRED COMPONENTS program_options thread filesystem iostreams regex)
IF (WIN32)
    ADD_DEFINITIONS("-DBOOST_ALL_NO_LIB")
    ADD_DEFINITIONS("-DBOOST_LIB_DIAGNOSTIC")
ENDIF (WIN32)

LINK_DIRECTORIES(${Boost_LIBRARY_DIRS})
INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIRS})

# Detect SeisComP API version
# For CMake we just need to know whether it's SeisComP3 or >=4.
# More detailed version checks are performed in the source code using
# preprocessor macros.
IF(EXISTS ${CMAKE_SOURCE_DIR}/src/trunk/libs/seiscomp3/core/version.h)
    # SeisComP3
    SET(SEISCOMP_BASE_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/trunk)
    SET(SEISCOMP_BASE_BINARY_DIR ${CMAKE_BINARY_DIR}/src/trunk)

    INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/src/system/libs)
    INCLUDE_DIRECTORIES(${CMAKE_BINARY_DIR}/src/system/libs)

    add_definitions(-D__MLA_SC3__)
ELSE()
    # SeisComP >= 4.0.0
    SET(SEISCOMP_BASE_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/base/common)
    SET(SEISCOMP_BASE_BINARY_DIR ${CMAKE_BINARY_DIR}/src/base/common)

    add_definitions(-D__MLA_SC_NEW__)
ENDIF()

INCLUDE_DIRECTORIES(${SEISCOMP_BASE_SOURCE_DIR}/libs)
INCLUDE_DIRECTORIES(${SEISCOMP_BASE_BINARY_DIR}/libs)

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/libs)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR}/libs)

SUBDIRS(plugins)
