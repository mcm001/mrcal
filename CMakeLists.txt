cmake_minimum_required(VERSION 3.23)

project(mrcal)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

add_library(mrcal 
    mrcal.cpp
)

target_include_directories(mrcal PRIVATE 
    ../libdogleg 
    ../suitesparse/CHOLMOD/Include
    ../suitesparse/SuiteSparse_config
)
