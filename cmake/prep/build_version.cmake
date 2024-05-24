# Check if env vars are defined before attempting to access them, variables will be defined even if blank
if((DEFINED ENV{BRANCH}) AND (DEFINED ENV{BUILD_VERSION}) AND (DEFINED ENV{COMMIT}))  # cmake-lint: disable=W0106
    if(($ENV{BRANCH} STREQUAL "master") AND (NOT $ENV{BUILD_VERSION} STREQUAL ""))
        # If BRANCH is "master" and BUILD_VERSION is not empty, then we are building a master branch
        MESSAGE("Got from CI master branch and version $ENV{BUILD_VERSION}")
        set(PROJECT_VERSION $ENV{BUILD_VERSION})
    elseif((DEFINED ENV{BRANCH}) AND (DEFINED ENV{COMMIT}))
        # If BRANCH is set but not BUILD_VERSION we are building a PR, we gather only the commit hash
        MESSAGE("Got from CI $ENV{BRANCH} branch and commit $ENV{COMMIT}")
        set(PROJECT_VERSION ${PROJECT_VERSION}.$ENV{COMMIT})
    endif()
    # Generate Sunshine Version based of the git tag
    # https://github.com/nocnokneo/cmake-git-versioning-example/blob/master/LICENSE
else()
    find_package(Git)
    if(GIT_EXECUTABLE)
        MESSAGE("${CMAKE_SOURCE_DIR}")
        get_filename_component(SRC_DIR "${CMAKE_SOURCE_DIR}" DIRECTORY)
        #Get current Branch
        execute_process(
                COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
                OUTPUT_VARIABLE GIT_DESCRIBE_BRANCH
                RESULT_VARIABLE GIT_DESCRIBE_ERROR_CODE
                OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        # Gather current commit
        execute_process(
                COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
                OUTPUT_VARIABLE GIT_DESCRIBE_VERSION
                RESULT_VARIABLE GIT_DESCRIBE_ERROR_CODE
                OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        # Check if Dirty
        execute_process(
                COMMAND ${GIT_EXECUTABLE} diff --quiet --exit-code
                RESULT_VARIABLE GIT_IS_DIRTY
                OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(NOT GIT_DESCRIBE_ERROR_CODE)
            MESSAGE("Sunshine Branch: ${GIT_DESCRIBE_BRANCH}")
            if(NOT GIT_DESCRIBE_BRANCH STREQUAL "master")
                set(PROJECT_VERSION ${PROJECT_VERSION}.${GIT_DESCRIBE_VERSION})
                MESSAGE("Sunshine Version: ${GIT_DESCRIBE_VERSION}")
            endif()
            if(GIT_IS_DIRTY)
                set(PROJECT_VERSION ${PROJECT_VERSION}.dirty)
                MESSAGE("Git tree is dirty!")
            endif()
        else()
            MESSAGE(ERROR ": Got git error while fetching tags: ${GIT_DESCRIBE_ERROR_CODE}")
        endif()
    else()
        MESSAGE(WARNING ": Git not found, cannot find git version")
    endif()
endif()
