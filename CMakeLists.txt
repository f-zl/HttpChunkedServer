cmake_minimum_required(VERSION 3.20)
project(http_chunked_server)
add_library(pico OBJECT picohttpparser.c picohttpparser.h)
add_executable(server
	http_chunked_server.c
	sock.c sock.h
	support.cpp support.h
)
target_compile_options(server PRIVATE -Wall -Wextra -pedantic -Wconversion -g)
target_link_libraries(server pico)
