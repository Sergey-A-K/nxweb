cmake_minimum_required(VERSION 3.12)
set(CMAKE_C_COMPILER "gcc") # gcc required
if(NOT CMAKE_BUILD_TYPE)
set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
    "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
    FORCE )
endif(NOT CMAKE_BUILD_TYPE)

project(nxweb)

set(WITH_PYTHON "OFF")
set(PACKAGE_VERSION "3.3.1-dev-cmake")
set(PACKAGE_STRING "${CMAKE_PROJECT_NAME} ${PACKAGE_VERSION}")

option(WITH_GNUTLS      "compile with SSL support" ON)
option(WITH_IMAGEMAGICK "compile with ImageMagick support" ON)
option(WITH_PYTHON2     "compile with Python2.7 support" ON)
option(WITH_PYTHON3     "compile with Python3.6 support" OFF)
option(WITH_GZIP        "compile with gzip encoding support" ON)
option(ENABLE_LOG_DEBUG "enable debug logging" ON)

set(WITH_SSL ${WITH_GNUTLS})
set(WITH_ZLIB ${WITH_GZIP})
add_compile_options("-D_GNU_SOURCE")

# some checks:

if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Linux")
message(FATAL_ERROR "nxweb only works on Linux")
endif (NOT ${CMAKE_SYSTEM_NAME} MATCHES "Linux")

if(NOT DEFINED CMAKE_INSTALL_BINDIR)
set(CMAKE_INSTALL_BINDIR ${CMAKE_INSTALL_PREFIX}/bin)
endif(NOT DEFINED CMAKE_INSTALL_BINDIR)

if(NOT DEFINED CMAKE_INSTALL_LIBDIR)
set(CMAKE_INSTALL_LIBDIR ${CMAKE_INSTALL_PREFIX}/lib)
endif(NOT DEFINED CMAKE_INSTALL_LIBDIR)

if(NOT DEFINED CMAKE_INSTALL_CONFDIR)
set(CMAKE_INSTALL_CONFDIR ${CMAKE_INSTALL_PREFIX}/etc)
endif(NOT DEFINED CMAKE_INSTALL_CONFDIR)

if(NOT DEFINED CMAKE_INSTALL_INCLUDEDIR)
set(CMAKE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_PREFIX}/include)
endif(NOT DEFINED CMAKE_INSTALL_INCLUDEDIR)

set(NXWEB_SYSCONFDIR ${CMAKE_INSTALL_CONFDIR}/nxweb)
set(NXWEB_LIBDIR ${CMAKE_INSTALL_LIBDIR}/nxweb)

include(CheckFunctionExists)

check_function_exists(register_printf_specifier USE_REGISTER_PRINTF_SPECIFIER)

check_function_exists(eventfd HAVE_EVENTFD)
if(NOT HAVE_EVENTFD)
message(FATAL_ERROR "eventfd() not available on this system")
endif (NOT HAVE_EVENTFD)

check_function_exists(clock_gettime HAVE_CLOCK_GETTIME)
if(NOT HAVE_CLOCK_GETTIME)
message(FATAL_ERROR "clock_gettime() not available on this system")
endif (NOT HAVE_CLOCK_GETTIME)


if(WITH_GZIP)
find_package(ZLIB REQUIRED)
if(NOT ZLIB_FOUND)
    message(SEND_ERROR "Failed to find ZLIB")
    return()
else()
    list(APPEND EXTRA_LIBS ${ZLIB_LIBRARIES})
    list(APPEND EXTRA_INCLUDES ${ZLIB_INCLUDE_DIRS})
endif()
endif(WITH_GZIP)


if(WITH_GNUTLS)
find_package(GnuTLS 3.0.12 REQUIRED)
if(NOT GNUTLS_FOUND)
    message(SEND_ERROR "Failed to find GnuTLS")
    return()
else()
    include_directories(${GnuTLS_INCLUDE_DIR})
    add_compile_options(${GNUTLS_DEFINITIONS})
    list(APPEND EXTRA_LIBS ${GNUTLS_LIBRARIES})
    list(APPEND EXTRA_INCLUDES ${GnuTLS_INCLUDE_DIR})
endif()
endif(WITH_GNUTLS)


if (WITH_IMAGEMAGICK)
find_package(ImageMagick 7.0.0 COMPONENTS MagickWand REQUIRED)
if(NOT ImageMagick_MagickWand_FOUND)
    message(SEND_ERROR "Failed to find ImageMagick/MagickWand")
    return()
else()
    list(APPEND EXTRA_LIBS ${ImageMagick_LIBRARIES} ${ImageMagick_MagickWand_LIBRARIES})
    list(APPEND EXTRA_INCLUDES ${ImageMagick_INCLUDE_DIRS} ${ImageMagick_MagickWand_INCLUDE_DIRS})
endif()
endif (WITH_IMAGEMAGICK)


if (WITH_PYTHON2)
find_package (Python2 COMPONENTS Interpreter Development REQUIRED)
if(NOT Python2_Development_FOUND)
    message(SEND_ERROR "Failed to find python2.7-dev")
    return()
else()
    set(WITH_PYTHON "ON")
    set(PYTHON_VERSION "2.7")

    list(APPEND EXTRA_LIBS ${Python2_LIBRARIES})
    list(APPEND EXTRA_INCLUDES ${Python2_INCLUDE_DIRS})

#     list(APPEND Python2_LIBRARIES ${PYTHON_LIBRARIES})
#     list(APPEND Python2_INCLUDE_DIRS ${PYTHON_INCLUDE_DIRS})
endif()
endif (WITH_PYTHON2)



if (WITH_PYTHON3)
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
if(NOT Python3_Development_FOUND)
    message(SEND_ERROR "Failed to find python3.6-dev")
    return()
else()
    set(WITH_PYTHON "ON")
    set(PYTHON_VERSION "3.6")

    list(APPEND EXTRA_LIBS ${Python3_LIBRARIES})
    list(APPEND EXTRA_INCLUDES ${Python3_INCLUDE_DIRS})

endif()
endif (WITH_PYTHON3)


#get_property(include_dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)

message(STATUS "Gzip Encoding:  ${WITH_GZIP}")
message(STATUS "GNUTLS:         ${WITH_GNUTLS} ${GNUTLS_DEFINITIONS}")
message(STATUS "ImageMagick:    ${WITH_IMAGEMAGICK}")
message(STATUS "Python2.7:      ${WITH_PYTHON2}")
message(STATUS "Python3.6:      ${WITH_PYTHON3}")
message(STATUS "EXTRA_INCLUDES: ${EXTRA_INCLUDES}")
message(STATUS "EXTRA_LIBS:     ${EXTRA_LIBS}")

configure_file (
"src/include/nxweb/config.h.cmake.in"
"${PROJECT_BINARY_DIR}/config.h"
)
include_directories("${PROJECT_BINARY_DIR}")


add_subdirectory(src/lib)
add_subdirectory(src/bin)
add_subdirectory(sample_config)


install(FILES "sample_config/python2/nxwebpy2.py" DESTINATION lib/nxweb)
install(FILES "sample_config/python3/nxwebpy3.py" DESTINATION lib/nxweb)
install(FILES "etc/nxweb_config.json" DESTINATION etc/nxweb)
install(FILES "${PROJECT_BINARY_DIR}/config.h" DESTINATION include/nxweb)


# build a CPack driven installer package
include (InstallRequiredSystemLibraries)
# set (CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/License.txt")
set(CPACK_PACKAGE_VERSION_MAJOR "3")
set(CPACK_PACKAGE_VERSION_MINOR "3")
set(CPACK_PACKAGE_VERSION_PATCH "1")
set(CPACK_SOURCE_IGNORE_FILES "/build/;/\\.hg/;/m4/;/autom4te.cache/;/dist/;/experimental/;/lib/;/nbproject/;/pkg/;/private/;/test/;/wiki/;/ssl/;/python2/.*/;/python3/.*/;\\\\.;.*~$;.*\\.in;${CPACK_SOURCE_IGNORE_FILES}")

# set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Yaroslav Stavnichiy <yarosla@gmail.com>")
# set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.17), libgnutls28 (>= 3.2.10-0), libmagickwand5 (>= 8:6.7.7.10), libpython2.7 (>= 2.7), libpython3.6 (>= 3.6)")
# #set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_SOURCE_DIR}/.../postinst;${CMAKE_SOURCE_DIR}/.../prerm;")
# SET(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
# SET(CPACK_DEBIAN_PACKAGE_SECTION "httpd")
# SET(CPACK_DEBIAN_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})

include (CPack)
