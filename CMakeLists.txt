cmake_minimum_required(VERSION 3.20)
project(http_chunked_server)
add_compile_options(-fsanitize=address)
add_link_options(-fsanitize=address)
set(cflags -Wall -Wextra -pedantic -Wconversion -g)

add_library(pico OBJECT picohttpparser.c picohttpparser.h)
add_library(support
	sock.c sock.h
	support.cpp support.h
)
target_compile_options(support PUBLIC ${cflags})
add_executable(http_chucnked_server
	http_chunked_server.c
)
target_link_libraries(http_chucnked_server pico support)

add_executable(server
	List.c List.h
	Server.c Server.h
	ServerApp.c ServerApp.h
)
target_link_libraries(http_chucnked_server pico support)
target_link_libraries(server pico support)

add_library(unity OBJECT unity.c)
add_executable(test test.c)
target_compile_options(test PRIVATE ${cflags})
target_link_libraries(test PRIVATE unity)
