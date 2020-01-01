# - Try to find FFMPEG
# Once done this will define
#  FFMPEG_FOUND - System has FFMPEG
#  FFMPEG_INCLUDE_DIRS - The FFMPEG include directories
#  FFMPEG_LIBRARIES - The libraries needed to use FFMPEG
#  FFMPEG_LIBRARY_DIRS - The directory to find FFMPEG libraries
#
# written by Roy Shilkrot 2013 http://www.morethantechnical.com/
#

find_package(PkgConfig)


MACRO(FFMPEG_FIND varname shortname headername)

  IF(NOT WIN32)
		PKG_CHECK_MODULES(PC_${varname} ${shortname})

		FIND_PATH(${varname}_INCLUDE_DIR "${shortname}/${headername}" 
			HINTS ${PC_${varname}_INCLUDEDIR} ${PC_${varname}_INCLUDE_DIRS}
			NO_DEFAULT_PATH
			)
	ELSE()
		FIND_PATH(${varname}_INCLUDE_DIR "${shortname}/${headername}")
	ENDIF()
		
	IF(${varname}_INCLUDE_DIR STREQUAL "${varname}_INCLUDE_DIR-NOTFOUND")
		message(STATUS "look for newer strcture")
		IF(NOT WIN32)
			PKG_CHECK_MODULES(PC_${varname} "lib${shortname}")

			FIND_PATH(${varname}_INCLUDE_DIR "lib${shortname}/${headername}"
				HINTS ${PC_${varname}_INCLUDEDIR} ${PC_${varname}_INCLUDE_DIRS}
				NO_DEFAULT_PATH
				)
		ELSE()
			FIND_PATH(${varname}_INCLUDE_DIR "lib${shortname}/${headername}")
			IF(${${varname}_INCLUDE_DIR} STREQUAL "${varname}_INCLUDE_DIR-NOTFOUND")
				#Desperate times call for desperate measures
				MESSAGE(STATUS "globbing...")
				FILE(GLOB_RECURSE ${varname}_INCLUDE_DIR "/ffmpeg*/${headername}")
				MESSAGE(STATUS "found: ${${varname}_INCLUDE_DIR}")
				IF(${varname}_INCLUDE_DIR)
					GET_FILENAME_COMPONENT(${varname}_INCLUDE_DIR "${${varname}_INCLUDE_DIR}" PATH)
					GET_FILENAME_COMPONENT(${varname}_INCLUDE_DIR "${${varname}_INCLUDE_DIR}" PATH)
				ELSE()
					SET(${varname}_INCLUDE_DIR "${varname}_INCLUDE_DIR-NOTFOUND")
				ENDIF()
			ENDIF()
		ENDIF()
	ENDIF()


	IF(${${varname}_INCLUDE_DIR} STREQUAL "${varname}_INCLUDE_DIR-NOTFOUND")
		MESSAGE(STATUS "Can't find includes for ${shortname}...")
	ELSE()
		MESSAGE(STATUS "Found ${shortname} include dirs: ${${varname}_INCLUDE_DIR}")

		#GET_DIRECTORY_PROPERTY(FFMPEG_PARENT DIRECTORY ${${varname}_INCLUDE_DIR} PARENT_DIRECTORY)
		GET_FILENAME_COMPONENT(FFMPEG_PARENT ${${varname}_INCLUDE_DIR} PATH)
		MESSAGE(STATUS "Using FFMpeg dir parent as hint: ${FFMPEG_PARENT}")

		IF(NOT WIN32)
			FIND_LIBRARY(${varname}_LIBRARIES NAMES ${shortname}
				HINTS ${PC_${varname}_LIBDIR} ${PC_${varname}_LIBRARY_DIR} ${FFMPEG_PARENT})
		ELSE()
			FIND_PATH(${varname}_LIBRARIES "${shortname}.dll.a" HINTS ${FFMPEG_PARENT})
			#			FILE(GLOB_RECURSE ${varname}_LIBRARIES "${FFMPEG_PARENT}/*${shortname}.lib") 
			# GLOBing is very bad... but windows sux, this is the only thing that works
		ENDIF()

		IF(${varname}_LIBRARIES STREQUAL "${varname}_LIBRARIES-NOTFOUND")
			MESSAGE(STATUS "look for newer structure for library")
			FIND_LIBRARY(${varname}_LIBRARIES NAMES lib${shortname}
				HINTS ${PC_${varname}_LIBDIR} ${PC_${varname}_LIBRARY_DIR} ${FFMPEG_PARENT})
		ENDIF()


		IF(${varname}_LIBRARIES STREQUAL "${varname}_LIBRARIES-NOTFOUND")
			MESSAGE(STATUS "Can't find lib for ${shortname}...")
		ELSE()
			MESSAGE(STATUS "Found ${shortname} libs: ${${varname}_LIBRARIES}")
		ENDIF()


		IF(NOT ${varname}_INCLUDE_DIR STREQUAL "${varname}_INCLUDE_DIR-NOTFOUND"
			AND NOT ${varname}_LIBRARIES STREQUAL ${varname}_LIBRARIES-NOTFOUND)

			MESSAGE(STATUS "found ${shortname}: include ${${varname}_INCLUDE_DIR} lib ${${varname}_LIBRARIES}")
			SET(FFMPEG_${varname}_FOUND 1)
			SET(FFMPEG_${varname}_INCLUDE_DIRS ${${varname}_INCLUDE_DIR})
			SET(FFMPEG_${varname}_LIBS ${${varname}_LIBRARIES})
		ELSE()
			MESSAGE(STATUS "Can't find ${shortname}")
		ENDIF()

	ENDIF()

ENDMACRO(FFMPEG_FIND)

FFMPEG_FIND(LIBAVFORMAT avformat avformat.h)
FFMPEG_FIND(LIBAVDEVICE avdevice avdevice.h)
FFMPEG_FIND(LIBAVCODEC  avcodec  avcodec.h)
FFMPEG_FIND(LIBAVUTIL   avutil   avutil.h)
FFMPEG_FIND(LIBSWSCALE  swscale  swscale.h)

SET(FFMPEG_FOUND "NO")
IF   (FFMPEG_LIBAVFORMAT_FOUND AND 
	FFMPEG_LIBAVDEVICE_FOUND AND 
	FFMPEG_LIBAVCODEC_FOUND AND 
	FFMPEG_LIBAVUTIL_FOUND AND
	FFMPEG_LIBSWSCALE_FOUND
	)


    SET(FFMPEG_FOUND "YES")

    SET(FFMPEG_INCLUDE_DIRS ${FFMPEG_LIBAVFORMAT_INCLUDE_DIRS})

    SET(FFMPEG_LIBRARY_DIRS ${FFMPEG_LIBAVFORMAT_LIBRARY_DIRS})

    SET(FFMPEG_LIBRARIES
        ${FFMPEG_LIBAVFORMAT_LIBS}
        ${FFMPEG_LIBAVDEVICE_LIBS}
        ${FFMPEG_LIBAVCODEC_LIBS}
        ${FFMPEG_LIBAVUTIL_LIBS}
	${FFMPEG_LIBSWSCALE_LIBS}
	)

ELSE ()

   MESSAGE(STATUS "Could not find FFMPEG")

ENDIF()

message(STATUS ${FFMPEG_LIBRARIES} ${FFMPEG_LIBAVFORMAT_LIBRARIES})

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set FFMPEG_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(FFMPEG DEFAULT_MSG
                                  FFMPEG_LIBRARIES FFMPEG_INCLUDE_DIRS)

mark_as_advanced(FFMPEG_INCLUDE_DIRS FFMPEG_LIBRARY_DIRS FFMPEG_LIBRARIES)
