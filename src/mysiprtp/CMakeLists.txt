cmake_minimum_required(VERSION 3.9.0)

project(mysiprtp)


if(APPLE)
	include(FindPkgConfig)
	pkg_search_module(PJSIP REQUIRED libpjproject)
	pkg_search_module(OPENSSL REQUIRED openssl)

	message(${PJSIP_INCLUDE_DIRS})
	message(${PJSIP_LIBRARIES})
	message(${PJSIP_LIBRARY_DIRS})

	set(CMAKE_EXE_LINKER_FLAGS
    		"-framework AudioToolbox -framework CoreAudio -framework VideoToolbox -framework CoreGraphics -framework QuartzCore -framework CoreFoundation -framework CoreMedia -framework Security")
#http://www.guanggua.com/question/26243169-CMake-target_include_directories-meaning-of-scope.html
endif()

link_directories(mysiprtp ${PJSIP_LIBRARY_DIRS} ${OPENSSL_LIBRARY_DIRS})

add_executable(mysiprtp
        mysip.c
        myrtp.c
	myrtp.h)

target_include_directories(mysiprtp PUBLIC ${PJSIP_INCLUDE_DIRS})
target_link_libraries(mysiprtp ${PJSIP_LIBRARIES})
