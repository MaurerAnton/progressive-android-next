cmake_minimum_required(VERSION 3.22.1)
project("sdfontlogin")

set(CMAKE_CXX_STANDARD 17)

find_library(log-lib log)
find_library(glesv3-lib GLESv3)
find_library(android-lib android)

add_library(sdfontlogin SHARED
    main.cpp
)

target_include_directories(sdfontlogin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(sdfontlogin
    ${glesv3-lib}
    ${log-lib}
    ${android-lib}
)
