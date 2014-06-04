cmake_minimum_required(VERSION 2.8)
project( FireFUSE )

SET(COMPILE_DEFINITIONS -Werror)
SET(FireFUSE_VERSION_MAJOR 0)
SET(FireFUSE_VERSION_MINOR 6)
SET(FireFUSE_VERSION_PATCH 1)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -g -D_FILE_OFFSET_BITS=64")
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_FILE_OFFSET_BITS=64")
SET(BUILD_TARGET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/target" CACHE STRING "Put all binary output in target")
SET(LIBRARY_OUTPUT_PATH "${BUILD_TARGET_DIR}")
SET(EXECUTABLE_OUTPUT_PATH "${BUILD_TARGET_DIR}")

include_directories( ${PROJECT_SOURCE_DIR} ${BUILD_TARGET_DIR} )
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include (InstallRequiredSystemLibraries)
include_directories(/usr/local/include)
get_property(dirs DIRECTORY . PROPERTY INCLUDE_DIRECTORIES)
message("INCLUDE_DIRECTORIES:${dirs}")

link_directories( ${BUILD_TARGET_DIR} /usr/local/lib /usr/lib )

SET(CPACK_RESOURCE_FILE_LICENSE  
     "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
SET(CPACK_PACKAGE_GENERATED_WARNING "GENERATED FILE!!! DO NOT CHANGE!!!")
SET(CPACK_PACKAGE_VERSION_MAJOR "${FireFUSE_VERSION_MAJOR}")
SET(CPACK_PACKAGE_VERSION_MINOR "${FireFUSE_VERSION_MINOR}")
SET(CPACK_PACKAGE_VERSION_PATCH "${FireFUSE_VERSION_PATCH}")
include (CPack)

configure_file (
  "${PROJECT_SOURCE_DIR}/version.h.in"
  "${PROJECT_BINARY_DIR}/version.h"
  )

find_package( OpenCV REQUIRED )

add_executable(firefuse 
  firerest.cpp
  fuse.c 
  background.cpp 
  cve.cpp 
  FireStep.cpp 
  )
target_link_libraries(firefuse libfirepicam.so lib_firesight.so libfuse.so ${OpenCV_LIBS})

add_executable(testfirefuse
  test/test.cpp
  #firerest.cpp
  #fuse.c 
  #background.cpp 
  #cve.cpp 
  #FireStep.cpp 
  )
target_link_libraries(testfirefuse lib_firesight.so )

INSTALL(TARGETS firefuse DESTINATION bin)
INSTALL(PROGRAMS mountfirefuse.sh DESTINATION /etc/init.d/)

