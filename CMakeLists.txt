cmake_minimum_required(VERSION 3.25)
project(OSProject2 C)

set(CMAKE_C_STANDARD 11)

add_executable(OSProject2 main.c)

find_package(OpenSSL REQUIRED)
target_link_libraries(OSProject2 OpenSSL::SSL OpenSSL::Crypto)
