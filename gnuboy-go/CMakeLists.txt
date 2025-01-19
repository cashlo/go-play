cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_options(-Wno-error=misleading-indentation)

project(gnuboy-go)