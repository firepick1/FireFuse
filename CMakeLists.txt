cmake_minimum_required(VERSION 2.8)
project( FireFuse )

SET(COMPILE_DEFINITIONS -Werror)
SET(FireFuse_VERSION_MAJOR 0)
SET(FireFuse_VERSION_MINOR 1)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -g")
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_FILE_OFFSET_BITS=64")

include (InstallRequiredSystemLibraries)
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

include_directories(/usr/local/include)
find_package( OpenCV REQUIRED )

add_executable(firefuse FireFuse.c FireStep.cpp FirePick.cpp )
target_link_libraries(firefuse libfirepicam.so libfiresight.so libfuse.so ${OpenCV_LIBS})

INSTALL(TARGETS firefuse DESTINATION bin)
INSTALL(PROGRAMS mountfirefuse.sh DESTINATION /etc/init.d/)

