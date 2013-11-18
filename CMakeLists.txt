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
SET(CPACK_PACKAGE_VERSION_MAJOR "${FireFuse_VERSION_MAJOR}")
SET(CPACK_PACKAGE_VERSION_MINOR "${FireFuse_VERSION_MINOR}")
include (CPack)

configure_file (
	"${PROJECT_SOURCE_DIR}/FireFuse.h.in"
	"${PROJECT_BINARY_DIR}/FireFuse.h"
	)

#include_directories(/usr/local/include)

add_executable(firefuse FireFuse.c FireLog.c FireStep.cpp FirePick.cpp)
target_link_libraries(firefuse /usr/local/lib/libfirepicam.so libfuse.so)

INSTALL(TARGETS firefuse DESTINATION bin)
INSTALL(PROGRAMS mountfirefuse.sh DESTINATION /etc/init.d/)

#OPENCV
#find_package( OpenCV REQUIRED )
#add_executable(fpc RaspiCamControl.c RaspiCLI.c RaspiPreview.c fpc.cpp)
#target_link_libraries(fpc /opt/vc/lib/libmmal_core.so /opt/vc/lib/libmmal_util.so /opt/vc/lib/libmmal_vc_client.so /opt/vc/lib/libvcos.so /opt/vc/lib/libbcm_host.so ${OpenCV_LIBS})

