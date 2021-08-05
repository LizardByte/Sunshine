cmake_minimum_required(VERSION 3.0)

project(Sunshine)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

add_subdirectory(third-party/Simple-Web-Server)

set(UPNPC_BUILD_SHARED OFF CACHE BOOL "no shared libraries")
set(UPNPC_BUILD_TESTS OFF CACHE BOOL "Don't build tests for miniupnpc")
set(UPNPC_BUILD_SAMPLE OFF CACHE BOOL "Don't build samples for miniupnpc")
set(UPNPC_NO_INSTALL ON CACHE BOOL "Don't install any libraries build for miniupnpc")
add_subdirectory(third-party/miniupnp/miniupnpc)
include_directories(third-party/miniupnp)

if(WIN32)
	# Ugly hack to compile with #include <qos2.h>
	add_compile_definitions(
		QOS_FLOWID=UINT32
		PQOS_FLOWID=UINT32*
		QOS_NON_ADAPTIVE_FLOW=2)
endif()
add_subdirectory(third-party/moonlight-common-c/enet)

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

	add_compile_definitions(SUNSHINE_PLATFORM="windows")
	add_subdirectory(tools) #This is temporary, only tools for Windows are needed, for now

	list(APPEND SUNSHINE_DEFINITIONS APPS_JSON="apps_windows.json")

	include_directories(third-party/ViGEmClient/include)

	set(PLATFORM_TARGET_FILES
		sunshine/platform/windows/publish.cpp
		sunshine/platform/windows/misc.h
		sunshine/platform/windows/misc.cpp
		sunshine/platform/windows/input.cpp
		sunshine/platform/windows/display.h
		sunshine/platform/windows/display_base.cpp
		sunshine/platform/windows/display_vram.cpp
		sunshine/platform/windows/display_ram.cpp
		sunshine/platform/windows/audio.cpp
		third-party/ViGEmClient/src/ViGEmClient.cpp
		third-party/ViGEmClient/include/ViGEm/Client.h
		third-party/ViGEmClient/include/ViGEm/Common.h
		third-party/ViGEmClient/include/ViGEm/Util.h
		third-party/ViGEmClient/include/ViGEm/km/BusShared.h)

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
		dnsapi
		)

	set_source_files_properties(third-party/ViGEmClient/src/ViGEmClient.cpp PROPERTIES COMPILE_DEFINITIONS "UNICODE=1;ERROR_INVALID_DEVICE_OBJECT_PARAMETER=650")
	set_source_files_properties(third-party/ViGEmClient/src/ViGEmClient.cpp PROPERTIES COMPILE_FLAGS "-Wno-unknown-pragmas -Wno-misleading-indentation -Wno-class-memaccess")
else()
	add_compile_definitions(SUNSHINE_PLATFORM="linux")
	list(APPEND SUNSHINE_DEFINITIONS APPS_JSON="apps_linux.json")

	find_package(X11)
	find_package(LIBDRM)

	find_package(FFMPEG REQUIRED)

	if(X11_FOUND)
		include_directories(${X11_INCLUDE_DIR})
		list(APPEND PLATFORM_TARGET_FILES sunshine/platform/linux/x11grab.cpp)
	elseif(LIBDRM_FOUND)
		include_directories(${LIBDRM_INCLUDE_DIRS})
		list(APPEND PLATFORM_LIBRARIES ${LIBDRM_LIBRARIES})
		list(APPEND PLATFORM_TARGET_FILES sunshine/platform/linux/kmsgrab.cpp)
		list(APPEND SUNSHINE_DEFINITIONS EGL_NO_X11=1)
	else()
		message(FATAL "Couldn't find either x11 or libdrm")
	endif()

	list(APPEND PLATFORM_TARGET_FILES
		sunshine/platform/linux/publish.cpp
		sunshine/platform/linux/vaapi.h
		sunshine/platform/linux/vaapi.cpp
		sunshine/platform/linux/graphics.h
		sunshine/platform/linux/graphics.cpp
		sunshine/platform/linux/misc.h
		sunshine/platform/linux/misc.cpp
		sunshine/platform/linux/audio.cpp
		sunshine/platform/linux/input.cpp
		third-party/glad/src/egl.c
		third-party/glad/src/gl.c
		third-party/glad/include/EGL/eglplatform.h
		third-party/glad/include/KHR/khrplatform.h
		third-party/glad/include/glad/gl.h
		third-party/glad/include/glad/egl.h)
		
	list(APPEND PLATFORM_LIBRARIES
		dl
		evdev
		pulse
		pulse-simple
		)
	
	include_directories(
		/usr/include/libevdev-1.0
		third-party/glad/include)

	if(NOT DEFINED SUNSHINE_EXECUTABLE_PATH)
		set(SUNSHINE_EXECUTABLE_PATH "sunshine")
	endif()
	configure_file(gen-deb.in gen-deb @ONLY)
	configure_file(sunshine.service.in sunshine.service @ONLY)
endif()

add_subdirectory(third-party/cbs)

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost COMPONENTS log filesystem REQUIRED)

set(SUNSHINE_TARGET_FILES
	third-party/moonlight-common-c/reedsolomon/rs.c
	third-party/moonlight-common-c/reedsolomon/rs.h
	third-party/moonlight-common-c/src/Input.h
	third-party/moonlight-common-c/src/Rtsp.h
	third-party/moonlight-common-c/src/RtspParser.c
	third-party/moonlight-common-c/src/Video.h
	sunshine/upnp.cpp
	sunshine/upnp.h
	sunshine/cbs.cpp
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
	sunshine/httpcommon.cpp
	sunshine/httpcommon.h
	sunshine/confighttp.cpp
	sunshine/confighttp.h
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

set_source_files_properties(sunshine/upnp.cpp PROPERTIES COMPILE_FLAGS -Wno-pedantic)

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/cbs/include
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/moonlight-common-c/enet/include
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/moonlight-common-c/reedsolomon
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

if(NOT SUNSHINE_CONFIG_DIR)
	set(SUNSHINE_CONFIG_DIR "${SUNSHINE_ASSETS_DIR}")
endif()

if(NOT SUNSHINE_DEFAULT_DIR)
	set(SUNSHINE_DEFAULT_DIR "${SUNSHINE_ASSETS_DIR}")
endif()

list(APPEND CBS_EXTERNAL_LIBRARIES
	cbs)

list(APPEND SUNSHINE_EXTERNAL_LIBRARIES
		libminiupnpc-static
		${CBS_EXTERNAL_LIBRARIES}
		${CMAKE_THREAD_LIBS_INIT}
		stdc++fs
		enet
		opus
		${FFMPEG_LIBRARIES}
		${Boost_LIBRARIES}
		${OPENSSL_LIBRARIES}
		${PLATFORM_LIBRARIES})

list(APPEND SUNSHINE_DEFINITIONS SUNSHINE_ASSETS_DIR="${SUNSHINE_ASSETS_DIR}")
list(APPEND SUNSHINE_DEFINITIONS SUNSHINE_CONFIG_DIR="${SUNSHINE_CONFIG_DIR}")
list(APPEND SUNSHINE_DEFINITIONS SUNSHINE_DEFAULT_DIR="${SUNSHINE_DEFAULT_DIR}")
add_executable(sunshine ${SUNSHINE_TARGET_FILES})
target_link_libraries(sunshine ${SUNSHINE_EXTERNAL_LIBRARIES})
target_compile_definitions(sunshine PUBLIC ${SUNSHINE_DEFINITIONS})
set_target_properties(sunshine PROPERTIES CXX_STANDARD 17)

target_compile_options(sunshine PRIVATE ${SUNSHINE_COMPILE_OPTIONS})
