cmake_minimum_required(VERSION 2.8)

project(Sunshine)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR})

# On MSYS2, building a stand-alone binary that links with ffmpeg is not possible,
# Therefore, ffmpeg, libx264 and libx265 must be build from source
if(WIN32)
	option(SUNSHINE_STANDALONE "Compile stand-alone binary of Sunshine" OFF)
	if(SUNSHINE_STANDALONE)
		set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
	
		if(NOT DEFINED SUNSHINE_PREPARED_BINARIES)
			set(SUNSHINE_PREPARED_BINARIES "${CMAKE_CURRENT_SOURCE_DIR}/pre-compiled/windows")
		endif()
		list(PREPEND PLATFORM_LIBRARIES
			C:/msys64/mingw64/lib/gcc/x86_64-w64-mingw32/${CMAKE_CXX_COMPILER_VERSION}/libstdc++.a
			C:/msys64/mingw64/x86_64-w64-mingw32/lib/libwinpthread.a
			)

		set(FFMPEG_INCLUDE_DIRS
			${SUNSHINE_PREPARED_BINARIES}/include)
		set(FFMPEG_LIBRARIES
			${SUNSHINE_PREPARED_BINARIES}/lib/libavcodec.a
			${SUNSHINE_PREPARED_BINARIES}/lib/libavdevice.a
			${SUNSHINE_PREPARED_BINARIES}/lib/libavfilter.a
			${SUNSHINE_PREPARED_BINARIES}/lib/libavformat.a
			${SUNSHINE_PREPARED_BINARIES}/lib/libavutil.a
			${SUNSHINE_PREPARED_BINARIES}/lib/libpostproc.a
			${SUNSHINE_PREPARED_BINARIES}/lib/libswresample.a
			${SUNSHINE_PREPARED_BINARIES}/lib/libswscale.a
			${SUNSHINE_PREPARED_BINARIES}/lib/libx264.a
			${SUNSHINE_PREPARED_BINARIES}/lib/libx265.a
			${SUNSHINE_PREPARED_BINARIES}/lib/libhdr10plus.a
			z lzma bcrypt C:/msys64/mingw64/lib/libiconv.a)
	endif()
else()
	set(SUNSHINE_STANDALONE OFF)
endif()

add_subdirectory(Simple-Web-Server)
add_subdirectory(moonlight-common-c/enet)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

if(NOT SUNSHINE_STANDALONE)
	find_package(FFmpeg REQUIRED)
endif()

list(APPEND SUNSHINE_COMPILE_OPTIONS -fPIC -Wall -Wno-missing-braces -Wno-maybe-uninitialized -Wno-sign-compare)

if(WIN32)
	add_subdirectory(tools) #This is temporary, only tools for Windows are needed, for now

	list(APPEND SUNSHINE_DEFINITIONS APPS_JSON="apps_windows.json")
	include_directories(
		ViGEmClient/include)
	set(PLATFORM_TARGET_FILES
		sunshine/platform/windows/input.cpp
		sunshine/platform/windows/display.h
		sunshine/platform/windows/display_base.cpp
		sunshine/platform/windows/display_vram.cpp
		sunshine/platform/windows/display_ram.cpp
		sunshine/platform/windows/audio.cpp
		ViGEmClient/src/ViGEmClient.cpp
		ViGEmClient/include/ViGEm/Client.h
		ViGEmClient/include/ViGEm/Common.h
		ViGEmClient/include/ViGEm/Util.h
		ViGEmClient/include/ViGEm/km/BusShared.h)
	list(PREPEND PLATFORM_LIBRARIES
		ssp
		winmm
		ksuser
		wsock32
		ws2_32
		iphlpapi
		d3d11 dxgi
		setupapi
		)

	set_source_files_properties(ViGEmClient/src/ViGEmClient.cpp PROPERTIES COMPILE_DEFINITIONS "UNICODE=1;ERROR_INVALID_DEVICE_OBJECT_PARAMETER=650")
	set_source_files_properties(ViGEmClient/src/ViGEmClient.cpp PROPERTIES COMPILE_FLAGS "-Wno-unknown-pragmas -Wno-misleading-indentation -Wno-class-memaccess")
else()
	list(APPEND SUNSHINE_DEFINITIONS APPS_JSON="apps_linux.json")

	find_package(X11 REQUIRED)
	set(PLATFORM_TARGET_FILES
		sunshine/platform/linux/display.cpp
		sunshine/platform/linux/input.cpp)
	
	set(PLATFORM_LIBRARIES
		Xfixes
		Xtst
		xcb
		xcb-shm
		xcb-xfixes
		${X11_LIBRARIES}
		evdev
		pulse
		pulse-simple
		)
	
	set(PLATFORM_INCLUDE_DIRS
		${X11_INCLUDE_DIR}
		/usr/include/libevdev-1.0)

	if(NOT DEFINED SUNSHINE_EXECUTABLE_PATH)
		set(SUNSHINE_EXECUTABLE_PATH "${CMAKE_CURRENT_BINARY_DIR}/sunshine")
	endif()
	configure_file(gen-deb.in gen-deb @ONLY)
	configure_file(sunshine.service.in sunshine.service @ONLY)
endif()

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost COMPONENTS log filesystem REQUIRED)

set(SUNSHINE_TARGET_FILES
	moonlight-common-c/reedsolomon/rs.c
	moonlight-common-c/reedsolomon/rs.h
	moonlight-common-c/src/Input.h
	moonlight-common-c/src/Rtsp.h
	moonlight-common-c/src/RtspParser.c
	moonlight-common-c/src/Video.h
	sunshine/utility.h
	sunshine/uuid.h
	sunshine/config.h
	sunshine/config.cpp
	sunshine/main.cpp
	sunshine/main.h
	sunshine/crypto.cpp
	sunshine/crypto.h
	sunshine/nvhttp.cpp
	sunshine/nvhttp.h
	sunshine/rtsp.cpp
	sunshine/rtsp.h
	sunshine/stream.cpp
	sunshine/stream.h
	sunshine/video.cpp
	sunshine/video.h
	sunshine/input.cpp
	sunshine/input.h
	sunshine/audio.cpp
	sunshine/audio.h
	sunshine/platform/common.h
	sunshine/process.cpp
	sunshine/process.h
	sunshine/network.cpp
	sunshine/network.h
	sunshine/move_by_copy.h
	sunshine/task_pool.h
	sunshine/thread_pool.h
	sunshine/thread_safe.h
	sunshine/sync.h
	sunshine/round_robin.h
	${PLATFORM_TARGET_FILES})

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/Simple-Web-Server
  ${CMAKE_CURRENT_SOURCE_DIR}/moonlight-common-c/enet/include
  ${CMAKE_CURRENT_SOURCE_DIR}/moonlight-common-c/reedsolomon
  ${FFMPEG_INCLUDE_DIRS}
  ${PLATFORM_INCLUDE_DIRS}
)

string(TOUPPER "x${CMAKE_BUILD_TYPE}" BUILD_TYPE)
if("${BUILD_TYPE}" STREQUAL "XDEBUG")
	list(APPEND SUNSHINE_COMPILE_OPTIONS -O0 -pedantic -ggdb3)
	if(WIN32)
		set_source_files_properties(sunshine/nvhttp.cpp PROPERTIES COMPILE_FLAGS -O2)
	endif()
else()
	add_definitions(-DNDEBUG)
	list(APPEND SUNSHINE_COMPILE_OPTIONS -O3)
endif()

if(NOT SUNSHINE_ASSETS_DIR)
	set(SUNSHINE_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")
endif()

if(SUNSHINE_STANDALONE)
	set(OPENSSL_LIBRARIES
		C:/msys64/mingw64/lib/libssl.a
		C:/msys64/mingw64/lib/libcrypto.a)
endif()

list(APPEND SUNSHINE_EXTERNAL_LIBRARIES
		${CMAKE_THREAD_LIBS_INIT}
		stdc++fs
		enet
		opus
		${FFMPEG_LIBRARIES}
		${Boost_LIBRARIES}
		${OPENSSL_LIBRARIES}
		${PLATFORM_LIBRARIES})

list(APPEND SUNSHINE_DEFINITIONS SUNSHINE_ASSETS_DIR="${SUNSHINE_ASSETS_DIR}")
add_executable(sunshine ${SUNSHINE_TARGET_FILES})
target_link_libraries(sunshine ${SUNSHINE_EXTERNAL_LIBRARIES})
target_compile_definitions(sunshine PUBLIC ${SUNSHINE_DEFINITIONS})
set_target_properties(sunshine PROPERTIES CXX_STANDARD 17)

target_compile_options(sunshine PRIVATE ${SUNSHINE_COMPILE_OPTIONS})
