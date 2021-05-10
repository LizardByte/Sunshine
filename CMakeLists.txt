cmake_minimum_required(VERSION 3.0)

project(Sunshine)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(Simple-Web-Server)

if(WIN32)
	# Ugly hack to compile with #include <qos2.h>
	add_compile_definitions(
		QOS_FLOWID=UINT32
		PQOS_FLOWID=UINT32*
		QOS_NON_ADAPTIVE_FLOW=2)
endif()
add_subdirectory(moonlight-common-c/enet)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

list(APPEND SUNSHINE_COMPILE_OPTIONS -fPIC -Wall -Wno-missing-braces -Wno-maybe-uninitialized -Wno-sign-compare)

if(WIN32)
	file(
		DOWNLOAD "https://github.com/TheElixZammuto/sunshine-prebuilt/releases/download/1.0.0/pre-compiled.zip" "${CMAKE_CURRENT_BINARY_DIR}/pre-compiled.zip"
		TIMEOUT 60
		EXPECTED_HASH SHA256=5d59986bd7f619eaaf82b2dd56b5127b747c9cbe8db61e3b898ff6b485298ed6)

	file(ARCHIVE_EXTRACT
		INPUT "${CMAKE_CURRENT_BINARY_DIR}/pre-compiled.zip"
		DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/pre-compiled)
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
	
	if(NOT DEFINED SUNSHINE_PREPARED_BINARIES)
		set(SUNSHINE_PREPARED_BINARIES "${CMAKE_CURRENT_BINARY_DIR}/pre-compiled/windows")
	endif()

	add_subdirectory(tools) #This is temporary, only tools for Windows are needed, for now

	list(APPEND SUNSHINE_DEFINITIONS APPS_JSON="apps_windows.json")

	include_directories(ViGEmClient/include)

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

	set(OPENSSL_LIBRARIES
		libssl.a
		libcrypto.a)

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
		z lzma bcrypt libiconv.a)

	list(PREPEND PLATFORM_LIBRARIES
		libstdc++.a
		libwinpthread.a
		libssp.a
		Qwave
		winmm
		ksuser
		wsock32
		ws2_32
		iphlpapi
		d3d11 dxgi D3DCompiler
		setupapi
		)

	set_source_files_properties(ViGEmClient/src/ViGEmClient.cpp PROPERTIES COMPILE_DEFINITIONS "UNICODE=1;ERROR_INVALID_DEVICE_OBJECT_PARAMETER=650")
	set_source_files_properties(ViGEmClient/src/ViGEmClient.cpp PROPERTIES COMPILE_FLAGS "-Wno-unknown-pragmas -Wno-misleading-indentation -Wno-class-memaccess")
else()
	list(APPEND SUNSHINE_DEFINITIONS APPS_JSON="apps_linux.json")

	find_package(X11 REQUIRED)
	find_package(FFmpeg REQUIRED)
	set(PLATFORM_TARGET_FILES
		sunshine/platform/linux/display.cpp
		sunshine/platform/linux/input.cpp)
	
	set(PLATFORM_LIBRARIES
		Xfixes
		Xtst
		xcb
		xcb-shm
		xcb-xfixes
		Xrandr
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
