cmake_minimum_required(VERSION 3.23)

project(mrcal)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

add_library(mrcal 
    mrcal.cpp
    cahvore.cpp
    poseutils-opencv.cpp
    poseutils-uses-autodiff.cc
    poseutils.cpp
    mrcal-opencv.cpp
)

target_include_directories(mrcal PRIVATE 
    ../libdogleg 
    ../suitesparse-windows-binaries/CHOLMOD/Include
    ../suitesparse-windows-binaries/SuiteSparse_config/include
)


add_executable(mrcal_test mrcal_cpp_main.cpp)

target_include_directories(mrcal_test PRIVATE 
    ../suitesparse-windows-binaries/CHOLMOD/Include
    ../suitesparse-windows-binaries/SuiteSparse_config/include
)

target_link_libraries(mrcal_test PUBLIC 
    mrcal
    D:/Documents/GitHub/suitesparse-windows-binaries/lib/Release/x64/suitesparseconfig.lib
    D:/Documents/GitHub/suitesparse-windows-binaries/lib/Release/x64/libcholmod.lib
    D:/Documents/GitHub/suitesparse-windows-binaries/lib/Release/x64/libccolamd.lib
    D:/Documents/GitHub/suitesparse-windows-binaries/lib/Release/x64/libcolamd.lib
    D:/Documents/GitHub/suitesparse-windows-binaries/lib/Release/x64/libcamd.lib
    D:/Documents/GitHub/suitesparse-windows-binaries/lib/Release/x64/libamd.lib
    D:/Documents/GitHub/suitesparse-windows-binaries/lib/Release/x64/metis.lib
    D:/Documents/GitHub/libdogleg/build/Debug/dogleg.lib
)
