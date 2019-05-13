#
# Copyright (C) The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

cmake_minimum_required(VERSION 3.4.1)

# build native_app_glue as a static lib
set(${CMAKE_C_FLAGS}, "${CMAKE_C_FLAGS}")
add_library(native_app_glue STATIC
        ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)

get_filename_component(
        PROJECT_SOURCE_DIR
        "${CMAKE_SOURCE_DIR}/.."
        ABSOLUTE)

# now build app's shared lib
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Werror")

# Export ANativeActivity_onCreate(),
# Refer to: https://github.com/android-ndk/ndk/issues/381.
set(CMAKE_SHARED_LINKER_FLAGS
        "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")

add_library(native-activity SHARED
        media_test.cpp
        sample_app.cpp
        util.cpp
        )

target_include_directories(native-activity PRIVATE
        ${PROJECT_SOURCE_DIR}/cpp
        ${ANDROID_NDK}/sources/android/native_app_glue)

# add lib dependencies
target_link_libraries(native-activity
        android
        mediandk
        native_app_glue
        log)

#
# Boost
#

set(BOOST_ROOT ${PROJECT_SOURCE_DIR}/third_party/boost)
set(Boost_INCLUDE_DIRS ${BOOST_ROOT})

target_include_directories(
        native-activity PRIVATE
        ${Boost_INCLUDE_DIRS})

add_custom_target(
        boost-init
        COMMAND ${PROJECT_SOURCE_DIR}/scripts/boost_init.sh
        WORKING_DIRECTORY ${BOOST_ROOT}
        VERBATIM)
add_dependencies(native-activity boost-init)
