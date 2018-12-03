cmake_minimum_required(VERSION 2.8)
project(spartan)

include_directories("$ENV{JAVA_HOME}/include/" "$ENV{JAVA_HOME}/include/linux/")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror -fPIC")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -fPIC -std=gnu++11 -static-libstdc++")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_DEBUG")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_DEBUG")
endif()

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath='\$ORIGIN/.'")

SET(CMAKE_SKIP_BUILD_RPATH TRUE)

set(SOURCE_FILES main.cpp)

set(LIB_SOURCE_FILES spartan.cpp spartan-exception.cpp findfiles.cpp ini.cpp cfgparse.cpp
    createjvm.cpp shm.cpp memBufStream.cpp fifo-pipe.cpp mq-queue.cpp send-mq-msg.cpp
    spartan-exception.cpp launch-program.cpp format2str.cpp log.cpp path-concat.cpp
    str-split.cpp session-state.cpp process-cmd-dispatch-info.cpp StdOutCapture.cpp)

if(DEFINED ENV{spartan_build_dir})
  set(spartan_BUILD_DIR "$ENV{spartan_build_dir}")
  set(CMAKE_BUILD_TYPE Release CACHE STRING "default to release for Maven builds" FORCE)
else()
  set(spartan_BUILD_DIR "${spartan_SOURCE_DIR}/build")
endif()

SET(LIBRARY_OUTPUT_PATH "${spartan_BUILD_DIR}/${CMAKE_BUILD_TYPE}")

SET(EXECUTABLE_OUTPUT_PATH "${LIBRARY_OUTPUT_PATH}")

add_library(spartan-shared SHARED ${LIB_SOURCE_FILES})

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

target_link_libraries(spartan-shared rt dl pthread popt)

target_link_libraries(${PROJECT_NAME} spartan-shared)

set_target_properties(${PROJECT_NAME} spartan-shared PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${EXECUTABLE_OUTPUT_PATH}"
    OUTPUT_NAME spartan
)

add_custom_command(TARGET spartan-shared POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:spartan-shared>" "${LIBRARY_OUTPUT_PATH}/$<TARGET_FILE_NAME:spartan-shared>"
    COMMENT "copying spartan-shared library to ${LIBRARY_OUTPUT_PATH}"
)
