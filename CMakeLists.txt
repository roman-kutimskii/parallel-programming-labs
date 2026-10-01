cmake_minimum_required(VERSION 3.20)
project(parallel_programming_labs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra)
endif()

# В libc++ (Clang, в том числе Apple Clang на macOS) <syncstream> пока считается
# экспериментальной частью библиотеки и без этого флага пуст: std::osyncstream
# не объявлен. Флаг нужен и при компиляции, и при линковке.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT MSVC)
    add_compile_options(-fexperimental-library)
    add_link_options(-fexperimental-library)
endif()

add_subdirectory(lab01-threads)
