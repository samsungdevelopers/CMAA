#[[
 Copyright (c) 2019, Arm Limited and Contributors

 SPDX-License-Identifier: Apache-2.0

 Licensed under the Apache License, Version 2.0 the "License";
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.

 ]]

cmake_minimum_required(VERSION 3.10)

set(SCRIPT_DIR ${CMAKE_CURRENT_LIST_DIR})
set(ROOT_DIR ${SCRIPT_DIR}/../..)

set(CMAKE_FILE ${SCRIPT_DIR}/template/sample/CMakeLists.txt.in)
set(SAMPLE_SOURCE_FILE ${SCRIPT_DIR}/template/sample/sample.cpp.in)
set(SAMPLE_HEADER_FILE ${SCRIPT_DIR}/template/sample/sample.h.in)

set(SAMPLE_NAME "" CACHE STRING "")
set(OUTPUT_DIR "${ROOT_DIR}/samples" CACHE PATH "")

function(convert_file_name)
    set(options)
    set(oneValueArgs INPUT OUTPUT)
    set(multiValueArgs)
    
    cmake_parse_arguments(TARGET "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # insert an underscore before any upper case letter
    string(REGEX REPLACE "(.)([A-Z][a-z]+)" "\\1_\\2" result ${TARGET_INPUT})
    # insert an underscore before any upper case letter
    string(REGEX REPLACE "([a-z0-9])([A-Z])" "\\1_\\2" result ${result})   
    # transform characters to lower case
    string(TOLOWER ${result} result)
    
    set(${TARGET_OUTPUT} ${result} PARENT_SCOPE)
endfunction()

if(NOT SAMPLE_NAME)
    message(FATAL_ERROR "Sample name cannot be empty.")
endif()

convert_file_name(
    INPUT ${SAMPLE_NAME} 
    OUTPUT SAMPLE_NAME_FILE)

configure_file(${CMAKE_FILE} ${OUTPUT_DIR}/${SAMPLE_NAME_FILE}/CMakeLists.txt @ONLY)
configure_file(${SAMPLE_SOURCE_FILE} ${OUTPUT_DIR}/${SAMPLE_NAME_FILE}/${SAMPLE_NAME_FILE}.cpp)
configure_file(${SAMPLE_HEADER_FILE} ${OUTPUT_DIR}/${SAMPLE_NAME_FILE}/${SAMPLE_NAME_FILE}.h)
