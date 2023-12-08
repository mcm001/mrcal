clang-cl /LD /MT -v -I D:\Documents\GitHub\vcpkg\installed\x64-windows\include\suitesparse -I ../libdogleg mrcal.c cahvore.cc poseutils-opencv.c poseutils-uses-autodiff.cc poseutils.c mrcal-opencv.c D:\Documents\GitHub\vcpkg\installed\x64-windows\lib\libcholmod.lib D:\Documents\GitHub\vcpkg\installed\x64-windows\lib\suitesparseconfig.lib D:\Documents\GitHub\vcpkg\installed\x64-windows\lib\lapack.lib ../libdogleg/dogleg.lib

clang-cl -o mrcal_test.exe test_mrcal.c mrcal.lib

@REM "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe" /MD -v -I D:\Documents\GitHub\vcpkg\installed\x64-windows\include\suitesparse -I ../libdogleg -o mrcal_test2.exe mrcal.c cahvore.cc poseutils-opencv.c poseutils-uses-autodiff.cc poseutils.c mrcal-opencv.c test_mrcal.c ../libdogleg/dogleg.c D:\Documents\GitHub\vcpkg\installed\x64-windows\lib\libcholmod.lib D:\Documents\GitHub\vcpkg\installed\x64-windows\lib\suitesparseconfig.lib D:\Documents\GitHub\vcpkg\installed\x64-windows\lib\lapack.lib

@REM setlocal
@REM SET PATH=%PATH%;D:\Documents\GitHub\vcpkg\installed\x64-windows\bin
@REM start mrcal_test2.exe
