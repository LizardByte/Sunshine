cmake_minimum_required(VERSION 2.8)

project(Sunshine)
# set up include-directories

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR})
add_subdirectory(Simple-Web-Server)
add_subdirectory(moonlight-common-c/enet)

find_package(X11 REQUIRED)
set(PLATFORM_TARGET_FILES
	sunshine/platform/linux.cpp
	sunshine/platform/linux_evdev.cpp)

set(PLATFORM_LIBRARIES
	Xfixes
	Xtst
	${X11_LIBRARIES}
	evdev
	pulse
	pulse-simple)

set(PLATFORM_INCLUDE_DIRS
	${X11_INCLUDE_DIR}
	/usr/include/libevdev-1.0)

set(SUNSHINE_TARGET_FILES
	moonlight-common-c/reedsolomon/rs.c
	moonlight-common-c/reedsolomon/rs.h
	moonlight-common-c/src/AudioStream.c
	moonlight-common-c/src/ByteBuffer.c
	moonlight-common-c/src/ByteBuffer.h
	moonlight-common-c/src/Connection.c
	moonlight-common-c/src/ControlStream.c
	moonlight-common-c/src/FakeCallbacks.c
	moonlight-common-c/src/Input.h
	moonlight-common-c/src/InputStream.c
	moonlight-common-c/src/Limelight.h
	moonlight-common-c/src/Limelight-internal.h
	moonlight-common-c/src/LinkedBlockingQueue.c
	moonlight-common-c/src/LinkedBlockingQueue.h
	moonlight-common-c/src/Misc.c
	moonlight-common-c/src/Platform.c
	moonlight-common-c/src/Platform.h
	moonlight-common-c/src/PlatformSockets.c
	moonlight-common-c/src/PlatformSockets.h
	moonlight-common-c/src/PlatformThreads.h
	moonlight-common-c/src/RtpFecQueue.c
	moonlight-common-c/src/RtpFecQueue.h
	moonlight-common-c/src/RtpReorderQueue.c
	moonlight-common-c/src/RtpReorderQueue.h
	moonlight-common-c/src/RtspConnection.c
	moonlight-common-c/src/Rtsp.h
	moonlight-common-c/src/RtspParser.c
	moonlight-common-c/src/SdpGenerator.c
	moonlight-common-c/src/SimpleStun.c
	moonlight-common-c/src/VideoDepacketizer.c
	moonlight-common-c/src/Video.h
	moonlight-common-c/src/VideoStream.c
	sunshine/utility.h
	sunshine/uuid.h
	sunshine/config.h
	sunshine/config.cpp
	sunshine/main.cpp
	sunshine/crypto.cpp
	sunshine/crypto.h
	sunshine/nvhttp.cpp
	sunshine/nvhttp.h
	sunshine/stream.cpp
	sunshine/stream.h
	sunshine/video.cpp
	sunshine/video.h
	sunshine/thread_safe.h
	sunshine/input.cpp
	sunshine/input.h
	sunshine/audio.cpp
	sunshine/audio.h
	sunshine/platform/common.h
	sunshine/process.cpp
	sunshine/process.h
	${PLATFORM_TARGET_FILES})

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/Simple-Web-Server
  ${CMAKE_CURRENT_SOURCE_DIR}/moonlight-common-c/enet/include
  ${CMAKE_CURRENT_SOURCE_DIR}/moonlight-common-c/reedsolomon
  ${FFMPEG_INCLUDE_DIRS}
  ${PLATFORM_INCLUDE_DIRS}
)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(FFmpeg REQUIRED)

list(APPEND SUNSHINE_COMPILE_OPTIONS -fPIC -Wall -Wno-missing-braces -Wno-maybe-uninitialized -Wno-sign-compare)
string(TOUPPER ${CMAKE_BUILD_TYPE} BUILD_TYPE)
if("x${BUILD_TYPE}" STREQUAL "xDEBUG")
	list(APPEND SUNSHINE_COMPILE_OPTIONS -O0 -pedantic -ggdb3)
else()
	add_definitions(-DNDEBUG)
	list(APPEND SUNSHINE_COMPILE_OPTIONS -O3)
endif()

list(APPEND SUNSHINE_EXTERNAL_LIBRARIES
		${CMAKE_THREAD_LIBS_INIT}
		${OPENSSL_LIBRARIES}
		enet
		opus
		${FFMPEG_LIBRARIES}
		${PLATFORM_LIBRARIES})

add_definitions(-DSUNSHINE_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
add_executable(sunshine ${SUNSHINE_TARGET_FILES})
target_link_libraries(sunshine ${SUNSHINE_EXTERNAL_LIBRARIES})
target_compile_definitions(sunshine PUBLIC ${SUNSHINE_DEFINITIONS})
set_target_properties(sunshine PROPERTIES CXX_STANDARD 17)

target_compile_options(sunshine PRIVATE ${SUNSHINE_COMPILE_OPTIONS})
