cmake_minimum_required(VERSION 3.0)

project(Sunshine VERSION 0.13.0)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

if(WIN32)
	# Ugly hack to compile with #include <qos2.h>
	add_compile_definitions(
		QOS_FLOWID=UINT32
		PQOS_FLOWID=UINT32*
		QOS_NON_ADAPTIVE_FLOW=2)
endif()
if(APPLE)
	macro(ADD_FRAMEWORK fwname appname)
    find_library(FRAMEWORK_${fwname}
    NAMES ${fwname}
    PATHS ${CMAKE_OSX_SYSROOT}/System/Library
    PATH_SUFFIXES Frameworks
    NO_DEFAULT_PATH)
    if( ${FRAMEWORK_${fwname}} STREQUAL FRAMEWORK_${fwname}-NOTFOUND)
        MESSAGE(ERROR ": Framework ${fwname} not found")
    else()
        TARGET_LINK_LIBRARIES(${appname} "${FRAMEWORK_${fwname}}/${fwname}")
        MESSAGE(STATUS "Framework ${fwname} found at ${FRAMEWORK_${fwname}}")
    endif()
	endmacro(ADD_FRAMEWORK)
endif()

add_subdirectory(third-party/moonlight-common-c/enet)
add_subdirectory(third-party/Simple-Web-Server)

set(UPNPC_BUILD_SHARED OFF CACHE BOOL "no shared libraries")
set(UPNPC_BUILD_TESTS OFF CACHE BOOL "Don't build tests for miniupnpc")
set(UPNPC_BUILD_SAMPLE OFF CACHE BOOL "Don't build samples for miniupnpc")
set(UPNPC_NO_INSTALL ON CACHE BOOL "Don't install any libraries build for miniupnpc")
add_subdirectory(third-party/miniupnp/miniupnpc)
include_directories(third-party/miniupnp)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
if(NOT APPLE)
	set(Boost_USE_STATIC_LIBS ON)
endif()
find_package(Boost COMPONENTS log filesystem REQUIRED)

list(APPEND SUNSHINE_COMPILE_OPTIONS -Wall -Wno-missing-braces -Wno-maybe-uninitialized -Wno-sign-compare)

if(WIN32)
	enable_language(RC)
	set(CMAKE_RC_COMPILER windres)
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

	if(NOT DEFINED SUNSHINE_ICON_PATH)
		set(SUNSHINE_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/sunshine.ico")
	endif()
	configure_file(sunshine/platform/windows/windows.rs.in windows.rc @ONLY)
	set(PLATFORM_TARGET_FILES
		"${CMAKE_CURRENT_BINARY_DIR}/windows.rc"
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
		ksuser
		wsock32
		ws2_32
		d3d11 dxgi D3DCompiler
		setupapi
		)

	set_source_files_properties(third-party/ViGEmClient/src/ViGEmClient.cpp PROPERTIES COMPILE_DEFINITIONS "UNICODE=1;ERROR_INVALID_DEVICE_OBJECT_PARAMETER=650")
	set_source_files_properties(third-party/ViGEmClient/src/ViGEmClient.cpp PROPERTIES COMPILE_FLAGS "-Wno-unknown-pragmas -Wno-misleading-indentation -Wno-class-memaccess")
elseif(APPLE)
	add_compile_definitions(SUNSHINE_PLATFORM="macos")
	list(APPEND SUNSHINE_DEFINITIONS APPS_JSON="apps_mac.json")
	link_directories(/opt/homebrew/lib/) # Default brew lib location
	link_directories(/opt/local/lib)
	ADD_DEFINITIONS(-DBOOST_LOG_DYN_LINK)

	find_package(FFmpeg REQUIRED)
	FIND_LIBRARY(APP_SERVICES_LIBRARY ApplicationServices )
	FIND_LIBRARY(AV_FOUNDATION_LIBRARY AVFoundation )
	FIND_LIBRARY(CORE_MEDIA_LIBRARY CoreMedia )
	FIND_LIBRARY(CORE_VIDEO_LIBRARY CoreVideo )
	FIND_LIBRARY(FOUNDATION_LIBRARY Foundation )
	list(APPEND SUNSHINE_EXTERNAL_LIBRARIES
		${APP_SERVICES_LIBRARY}
		${AV_FOUNDATION_LIBRARY}
		${CORE_MEDIA_LIBRARY}
		${CORE_VIDEO_LIBRARY}
		${FOUNDATION_LIBRARY})

	set(PLATFORM_INCLUDE_DIRS
		${Boost_INCLUDE_DIR})

	set(PLATFORM_TARGET_FILES
		sunshine/platform/macos/av_audio.h
		sunshine/platform/macos/av_audio.m
		sunshine/platform/macos/av_img_t.h
		sunshine/platform/macos/av_video.h
		sunshine/platform/macos/av_video.m
		sunshine/platform/macos/display.mm
		sunshine/platform/macos/input.cpp
		sunshine/platform/macos/microphone.mm
		sunshine/platform/macos/misc.cpp
		sunshine/platform/macos/misc.h
		sunshine/platform/macos/nv12_zero_device.cpp
		sunshine/platform/macos/nv12_zero_device.h
		sunshine/platform/macos/publish.cpp
		sunshine/platform/macos/TPCircularBuffer/TPCircularBuffer.c
		sunshine/platform/macos/TPCircularBuffer/TPCircularBuffer.h
		${CMAKE_CURRENT_SOURCE_DIR}/assets/Info.plist)
else()
	add_compile_definitions(SUNSHINE_PLATFORM="linux")
	list(APPEND SUNSHINE_DEFINITIONS APPS_JSON="apps_linux.json")

	option(SUNSHINE_ENABLE_DRM "Enable KMS grab if available" ON)
	option(SUNSHINE_ENABLE_X11 "Enable X11 grab if available" ON)
	option(SUNSHINE_ENABLE_WAYLAND "Enable building wayland specific code" ON)
	option(SUNSHINE_ENABLE_CUDA "Enable cuda specific code" ON)

	if(${SUNSHINE_ENABLE_X11})
		find_package(X11)
	else()
		set(X11_FOUND OFF)
	endif()

	set(CUDA_FOUND OFF)
	if(${SUNSHINE_ENABLE_CUDA})
		include(CheckLanguage)
		check_language(CUDA)

		if(CMAKE_CUDA_COMPILER)
			if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
				set(CMAKE_CUDA_ARCHITECTURES 35)
			endif()

			set(CUDA_FOUND ON)
			enable_language(CUDA)
		endif()
	endif()
	if(${SUNSHINE_ENABLE_DRM})
		find_package(LIBDRM)
		find_package(LIBCAP)
	else()
		set(LIBDRM_FOUND OFF)
		set(LIBCAP_FOUND OFF)
	endif()
	if(${SUNSHINE_ENABLE_WAYLAND})
		find_package(Wayland)
	else()
		set(WAYLAND_FOUND OFF)
	endif()

	find_package(FFMPEG REQUIRED)

	if(X11_FOUND)
		add_compile_definitions(SUNSHINE_BUILD_X11)
		include_directories(${X11_INCLUDE_DIR})
		list(APPEND PLATFORM_TARGET_FILES sunshine/platform/linux/x11grab.cpp)
	endif()

	if(CUDA_FOUND)
		include_directories(third-party/nvfbc)
		list(APPEND PLATFORM_TARGET_FILES
			sunshine/platform/linux/cuda.cu
			sunshine/platform/linux/cuda.cpp
			third-party/nvfbc/NvFBC.h)

			add_compile_definitions(SUNSHINE_BUILD_CUDA)
	endif()

	if(LIBDRM_FOUND AND LIBCAP_FOUND)
		add_compile_definitions(SUNSHINE_BUILD_DRM)
		include_directories(${LIBDRM_INCLUDE_DIRS} ${LIBCAP_INCLUDE_DIRS})
		list(APPEND PLATFORM_LIBRARIES ${LIBDRM_LIBRARIES} ${LIBCAP_LIBRARIES})
		list(APPEND PLATFORM_TARGET_FILES sunshine/platform/linux/kmsgrab.cpp)
		list(APPEND SUNSHINE_DEFINITIONS EGL_NO_X11=1)
	elseif(LIBDRM_FOUND)
		message(WARNING "Found libdrm, yet there is no libcap")
	elseif(LIBDRM_FOUND)
		message(WARNING "Found libcap, yet there is no libdrm")
	endif()

	if(WAYLAND_FOUND)
		add_compile_definitions(SUNSHINE_BUILD_WAYLAND)
		macro(genWayland FILENAME)
			make_directory(${CMAKE_BINARY_DIR}/generated-src)

			message("wayland-scanner private-code ${CMAKE_SOURCE_DIR}/third-party/wayland-protocols/${FILENAME}.xml ${CMAKE_BINARY_DIR}/generated-src/${FILENAME}.c")
			message("wayland-scanner client-header ${CMAKE_SOURCE_DIR}/third-party/wayland-protocols/${FILENAME}.xml ${CMAKE_BINARY_DIR}/generated-src/${FILENAME}.h")
			execute_process(
				COMMAND wayland-scanner private-code ${CMAKE_SOURCE_DIR}/third-party/wayland-protocols/${FILENAME}.xml ${CMAKE_BINARY_DIR}/generated-src/${FILENAME}.c
				COMMAND wayland-scanner client-header ${CMAKE_SOURCE_DIR}/third-party/wayland-protocols/${FILENAME}.xml ${CMAKE_BINARY_DIR}/generated-src/${FILENAME}.h

				RESULT_VARIABLE EXIT_INT
			)

			if(NOT ${EXIT_INT} EQUAL 0)
				message(FATAL_ERROR "wayland-scanner failed")
			endif()

			list(APPEND PLATFORM_TARGET_FILES
				${CMAKE_BINARY_DIR}/generated-src/${FILENAME}.c
				${CMAKE_BINARY_DIR}/generated-src/${FILENAME}.h
			)
		endmacro()

		genWayland(xdg-output-unstable-v1)
		genWayland(wlr-export-dmabuf-unstable-v1)

		include_directories(
			${WAYLAND_INCLUDE_DIRS}
			${CMAKE_BINARY_DIR}/generated-src
		)

		list(APPEND PLATFORM_LIBRARIES ${WAYLAND_LIBRARIES})
		list(APPEND PLATFORM_TARGET_FILES
			sunshine/platform/linux/wlgrab.cpp
			sunshine/platform/linux/wayland.cpp)
	endif()
	if(NOT ${X11_FOUND} AND NOT (${LIBDRM_FOUND} AND ${LIBCAP_FOUND}) AND NOT ${WAYLAND_FOUND} AND NOT ${})
		message(FATAL_ERROR "Couldn't find either x11, wayland, cuda or (libdrm and libcap)")
	endif()

	list(APPEND PLATFORM_TARGET_FILES
		sunshine/platform/linux/publish.cpp
		sunshine/platform/linux/vaapi.h
		sunshine/platform/linux/vaapi.cpp
		sunshine/platform/linux/cuda.h
		sunshine/platform/linux/graphics.h
		sunshine/platform/linux/graphics.cpp
		sunshine/platform/linux/misc.h
		sunshine/platform/linux/misc.cpp
		sunshine/platform/linux/audio.cpp
		sunshine/platform/linux/input.cpp
		sunshine/platform/linux/x11grab.h
		sunshine/platform/linux/wayland.h
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
		third-party/nv-codec-headers/include
		third-party/glad/include)

	if(NOT DEFINED SUNSHINE_EXECUTABLE_PATH)
		set(SUNSHINE_EXECUTABLE_PATH "sunshine")
	endif()
	configure_file(sunshine.desktop.in sunshine.desktop @ONLY)
	configure_file(sunshine.service.in sunshine.service @ONLY)
endif()

configure_file(version.h.in version.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

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

add_subdirectory(third-party/cbs)

string(TOUPPER "x${CMAKE_BUILD_TYPE}" BUILD_TYPE)
if("${BUILD_TYPE}" STREQUAL "XDEBUG")
	list(APPEND SUNSHINE_COMPILE_OPTIONS -O0 -ggdb3)
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
		enet
		opus
		${FFMPEG_LIBRARIES}
		${Boost_LIBRARIES}
		${OPENSSL_LIBRARIES}
		${PLATFORM_LIBRARIES})

if (NOT WIN32)
	list(APPEND SUNSHINE_EXTERNAL_LIBRARIES Boost::log)
endif()

list(APPEND SUNSHINE_DEFINITIONS SUNSHINE_ASSETS_DIR="${SUNSHINE_ASSETS_DIR}")
list(APPEND SUNSHINE_DEFINITIONS SUNSHINE_CONFIG_DIR="${SUNSHINE_CONFIG_DIR}")
list(APPEND SUNSHINE_DEFINITIONS SUNSHINE_DEFAULT_DIR="${SUNSHINE_DEFAULT_DIR}")
add_executable(sunshine ${SUNSHINE_TARGET_FILES})
target_link_libraries(sunshine ${SUNSHINE_EXTERNAL_LIBRARIES} ${EXTRA_LIBS})
target_compile_definitions(sunshine PUBLIC ${SUNSHINE_DEFINITIONS})
set_target_properties(sunshine PROPERTIES CXX_STANDARD 17
                            VERSION ${PROJECT_VERSION}
                            SOVERSION ${PROJECT_VERSION_MAJOR}
                            )

if(NOT DEFINED CMAKE_CUDA_STANDARD)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
endif()

if(APPLE)
	target_link_options(sunshine PRIVATE LINKER:-sectcreate,__TEXT,__info_plist,${CMAKE_CURRENT_SOURCE_DIR}/assets/Info.plist)
endif()

foreach(flag IN LISTS SUNSHINE_COMPILE_OPTIONS)
	list(APPEND SUNSHINE_COMPILE_OPTIONS_CUDA "$<$<COMPILE_LANGUAGE:CUDA>:--compiler-options=${flag}>")
endforeach()

target_compile_options(sunshine PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${SUNSHINE_COMPILE_OPTIONS}>;$<$<COMPILE_LANGUAGE:CUDA>:${SUNSHINE_COMPILE_OPTIONS_CUDA};-std=c++17>)

#############
# CPACK
####

# Common options
set(CPACK_PACKAGE_NAME "SunshineStream")
set(CPACK_PACKAGE_VENDOR "CMake.org")
set(CPACK_PACKAGE_CONTACT "https://github.com/SunshineStream/Sunshine")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "https://github.com/SunshineStream/Sunshine")
set(CPACK_PACKAGE_DESCRIPTION "Gamestream host for Moonlight")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://sunshinestream.github.io")
set(CPACK_RESOURCE_FILE_LICENSE ${PROJECT_SOURCE_DIR}/LICENSE)
set(CPACK_PACKAGE_ICON ${PROJECT_SOURCE_DIR}/sunshine.png)
set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}")
set(CPACK_STRIP_FILES YES)

# Platform specific options
if(WIN32) # see options at: https://cmake.org/cmake/help/latest/cpack_gen/nsis.html
	install(TARGETS sunshine RUNTIME DESTINATION "." COMPONENT application)

	# Adding tools
	install(TARGETS dxgi-info RUNTIME DESTINATION "tools" COMPONENT dxgi)
	install(TARGETS audio-info RUNTIME DESTINATION "tools" COMPONENT audio)
	install(TARGETS sunshinesvc RUNTIME DESTINATION "tools" COMPONENT sunshinesvc)

	install(DIRECTORY "${SUNSHINE_ASSETS_DIR}/web" DESTINATION "${SUNSHINE_CONFIG_DIR}" COMPONENT web)
	install(FILES "${SUNSHINE_ASSETS_DIR}/apps_windows.json" DESTINATION "${SUNSHINE_CONFIG_DIR}" COMPONENT assets)
	install(FILES "${SUNSHINE_ASSETS_DIR}/sunshine.conf" DESTINATION "${SUNSHINE_CONFIG_DIR}" COMPONENT assets)
	install(DIRECTORY "${SUNSHINE_ASSETS_DIR}/shaders/directx" DESTINATION "${SUNSHINE_CONFIG_DIR}/shaders" COMPONENT assets)

	set(CPACK_PACKAGE_ICON  "${CMAKE_CURRENT_SOURCE_DIR}\\\\sunshine.ico")
	set(CPACK_NSIS_INSTALLED_ICON_NAME  "${PROJECT__DIR}\\\\${PROJECT_EXE}")
	set(CPACK_PACKAGE_INSTALL_DIRECTORY "${CPACK_PACKAGE_NAME}") # The name of the directory that will be created in C:/Program files/
	string(APPEND CPACK_NSIS_DEFINES "\n  RequestExecutionLevel admin") # TODO: Not sure if this is needed but it took me a while to figure out where to put this option so I'm leaving it here

	# Sets permissions on the installed folder so that we can write in it
	SET(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
			"${CPACK_NSIS_EXTRA_INSTALL_COMMANDS}
    ExecWait 'icacls \\\"$INSTDIR\\\" /grant:r Users:\\\(OI\\\)\\\(CI\\\)\\\(F\\\)'
    ")

	# Adding an option for the start menu and PATH
	set(CPACK_NSIS_MODIFY_PATH "OFF") # TODO: it asks to add it to the PATH but then it seems I can't just run it from powershell
	set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
	set(CPACK_NSIS_MUI_FINISHPAGE_RUN "${CMAKE_PROJECT_NAME}.exe")
	set(CPACK_NSIS_INSTALLED_ICON_NAME "${CMAKE_PROJECT_NAME}.exe") # This will be shown on the installed apps Windows settings
	set(CPACK_NSIS_CREATE_ICONS "CreateShortCut '\$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\${CMAKE_PROJECT_NAME}.lnk' '\$INSTDIR\\\\${CMAKE_PROJECT_NAME}.exe'")

	# Checking for previous installed versions
	# set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL "ON") # TODO: doesn't work on my machine when Sunshine is already installed

	# Setting components groups and dependencies
	set(CPACK_COMPONENT_APPLICATION_DISPLAY_NAME "${CMAKE_PROJECT_NAME}")
	set(CPACK_COMPONENT_WEB_DISPLAY_NAME "Web interface")
	set(CPACK_COMPONENT_ASSETS_DISPLAY_NAME "Extra assets files")
	set(CPACK_COMPONENT_LIBRARIES_DISPLAY_NAME "Libraries")

	set(CPACK_COMPONENT_APPLICATION_GROUP "Runtime")
	set(CPACK_COMPONENT_WEB_GROUP "Runtime")
	set(CPACK_COMPONENT_ASSETS_GROUP "Runtime")

	set(CPACK_COMPONENT_DXGI_DISPLAY_NAME "dxgi-info.exe")
	set(CPACK_COMPONENT_AUDIO_DISPLAY_NAME "audio-info.exe")
	set(CPACK_COMPONENT_SUNSHINESVC_DISPLAY_NAME "sunshinesvc.exe")

	set(CPACK_COMPONENT_DXGI_GROUP "Extra Tools")
	set(CPACK_COMPONENT_AUDIO_GROUP "Extra Tools")
	set(CPACK_COMPONENT_SUNSHINESVC_GROUP "Extra Tools")

	set(CPACK_COMPONENT_APPLICATION_DEPENDS web assets)
endif()
if(APPLE) # TODO: test

	set(prefix "${CMAKE_PROJECT_NAME}.app/Contents")
	set(INSTALL_RUNTIME_DIR "${prefix}/MacOS")

	install(DIRECTORY "${SUNSHINE_ASSETS_DIR}/web" DESTINATION "${INSTALL_RUNTIME_DIR}")
	install(FILES "${SUNSHINE_ASSETS_DIR}/sunshine.conf" DESTINATION "${INSTALL_RUNTIME_DIR}")

	install(TARGETS sunshine
			BUNDLE DESTINATION . COMPONENT Runtime
			RUNTIME DESTINATION ${INSTALL_RUNTIME_DIR} COMPONENT Runtime)

	# TODO: bundle doesn't produce a valid .app use cpack -G DragNDrop
	set(CPACK_BUNDLE_NAME "${CMAKE_PROJECT_NAME}")
	set(CPACK_BUNDLE_PLIST "${SUNSHINE_ASSETS_DIR}/info.plist")
	set(CPACK_BUNDLE_ICON "${PROJECT_SOURCE_DIR}/sunshine.icns")
endif()
if(UNIX AND NOT APPLE)
	install(DIRECTORY "${SUNSHINE_ASSETS_DIR}/web" DESTINATION "${SUNSHINE_CONFIG_DIR}")
	install(FILES "${SUNSHINE_ASSETS_DIR}/sunshine.conf" DESTINATION "${SUNSHINE_CONFIG_DIR}")

	install(FILES "${SUNSHINE_ASSETS_DIR}/apps_linux.json" DESTINATION "${SUNSHINE_CONFIG_DIR}")
	install(FILES "${SUNSHINE_ASSETS_DIR}/85-sunshine-rules.rules" DESTINATION "/etc/udev/rules.d")
	install(TARGETS sunshine RUNTIME DESTINATION "/usr/bin")
	install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sunshine.service" DESTINATION "/usr/lib/systemd/user")
	install(DIRECTORY "${SUNSHINE_ASSETS_DIR}/shaders/opengl" DESTINATION "${SUNSHINE_CONFIG_DIR}/shaders")

	# Pre and post install
	set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
		"${SUNSHINE_ASSETS_DIR}/linux-deb/preinst;${SUNSHINE_ASSETS_DIR}/linux-deb/postinst;${SUNSHINE_ASSETS_DIR}/linux-deb/conffiles")
	set(CPACK_RPM_PRE_INSTALL_SCRIPT_FILE "${SUNSHINE_ASSETS_DIR}/linux-deb/preinst")
	set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${SUNSHINE_ASSETS_DIR}/linux-deb/postinst")

	# Dependencies
	set(CPACK_DEB_COMPONENT_INSTALL ON)
	set(CPACK_DEBIAN_PACKAGE_DEPENDS "libssl1.1, libavdevice58, libboost-thread1.67.0 | libboost-thread1.71.0 | libboost-thread1.74.0, libboost-filesystem1.67.0 | libboost-filesystem1.71.0 | libboost-filesystem1.74.0, libboost-log1.67.0 | libboost-log1.71.0 | libboost-log1.74.0, libpulse0, libopus0, libxcb-shm0, libxcb-xfixes0, libxtst6, libevdev2, libdrm2, libcap2")
	set(CPACK_RPM_PACKAGE_REQUIRES "libssl==1.1, libavdevice>=58, libboost-thread>=1.67.0, libboost-filesystem>=1.67.0, libboost-log>=1.67.0, libpulse>=0, libopus>=0, libxcb-shm>=0, libxcb-xfixes>=0, libxtst>=0, libevdev>=2.0, libdrm>=2.0, libcap>=2.0")
	set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF) # This should automatically figure out dependencies, doesn't work with the current config

	# Installation destination dir
	set(CPACK_SET_DESTDIR true)
	set(CMAKE_INSTALL_PREFIX "/etc/sunshine")
endif()

include(CPack)