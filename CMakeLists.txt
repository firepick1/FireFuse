cmake_minimum_required(VERSION 2.8)
project( FireFuse )

SET(COMPILE_DEFINITIONS -Werror)
SET(FireFuse_VERSION_MAJOR 0)
SET(FireFuse_VERSION_MINOR 1)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -g")
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_FILE_OFFSET_BITS=64")

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include (InstallRequiredSystemLibraries)
include_directories(/usr/local/include)
get_property(dirs DIRECTORY . PROPERTY INCLUDE_DIRECTORIES)
message("INCLUDE_DIRECTORIES:${dirs}")

link_directories( ${BUILD_TARGET_DIR} /usr/local/lib /usr/lib )

SET(CPACK_RESOURCE_FILE_LICENSE  
     "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
SET(CPACK_PACKAGE_GENERATED_WARNING "GENERATED FILE!!! DO NOT CHANGE!!!")
SET(CPACK_PACKAGE_VERSION_MAJOR "${FireFuse_VERSION_MAJOR}")
SET(CPACK_PACKAGE_VERSION_MINOR "${FireFuse_VERSION_MINOR}")
include (CPack)

configure_file (
	"${PROJECT_SOURCE_DIR}/version.h.in"
	"${PROJECT_BINARY_DIR}/version.h"
	)

find_package( OpenCV REQUIRED )

add_executable(firefuse FireFuse.c FireLog.cpp FireStep.cpp FirePick.cpp )
target_link_libraries(firefuse libfirepicam.so lib_firesight.so libfuse.so ${OpenCV_LIBS})

INSTALL(TARGETS firefuse DESTINATION bin)
INSTALL(PROGRAMS mountfirefuse.sh DESTINATION /etc/init.d/)

