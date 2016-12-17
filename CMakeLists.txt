cmake_minimum_required(VERSION 2.8)
project(aftp)

include_directories(${CMAKE_SOURCE_DIR}/include)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c99")

add_library(ftp_server src/srv/server.c src/srv/srv_sm.c)
add_library(ftp_client src/client/client.c src/client/sm.c src/helpers.c)

add_definitions(-Wall)

add_executable(aftp src/main.c)
target_link_libraries(aftp ftp_server ftp_client)