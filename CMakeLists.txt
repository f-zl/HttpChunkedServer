cmake_minimum_required(VERSION 3.20)
project(http_chunked_server)
add_compile_options(-fsanitize=address)
add_link_options(-fsanitize=address)
set(cflags -Wall -Wextra -pedantic -Wconversion -g)

add_library(pico OBJECT picohttpparser.c picohttpparser.h)
add_executable(server
	http_chunked_server.c
	sock.c sock.h
	support.cpp support.h
)
target_compile_options(server PRIVATE ${cflags})
target_link_libraries(server pico)
add_library(unity OBJECT unity.c)
add_executable(test test.c)
target_compile_options(test PRIVATE ${cflags})
target_link_libraries(test PRIVATE unity)
