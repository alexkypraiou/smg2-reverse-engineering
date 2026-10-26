cmake_minimum_required(VERSION 3.20)
project(smg2_native LANGUAGES CXX)

add_library(smg2_cube_gravity src/cube_gravity.cpp src/planet_gravity.cpp src/point_gravity.cpp)
target_include_directories(smg2_cube_gravity PUBLIC include)
target_compile_features(smg2_cube_gravity PUBLIC cxx_std_20)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(smg2_cube_gravity PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

enable_testing()
add_executable(cube_gravity_tests tests/cube_gravity_tests.cpp)
target_link_libraries(cube_gravity_tests PRIVATE smg2_cube_gravity)
target_compile_features(cube_gravity_tests PRIVATE cxx_std_20)
add_test(NAME cube_gravity_tests COMMAND cube_gravity_tests)

add_executable(planet_gravity_tests tests/planet_gravity_tests.cpp)
target_link_libraries(planet_gravity_tests PRIVATE smg2_cube_gravity)
add_test(NAME planet_gravity_tests COMMAND planet_gravity_tests)

add_library(smg2_data src/data/yaz0.cpp src/data/rarc.cpp src/data/u8.cpp src/data/bcsv.cpp src/data/fst.cpp src/data/j3d.cpp)
target_include_directories(smg2_data PUBLIC include)
target_compile_features(smg2_data PUBLIC cxx_std_20)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(smg2_data PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_executable(archive_format_tests tests/data/archive_format_tests.cpp)
target_link_libraries(archive_format_tests PRIVATE smg2_data)
add_test(NAME archive_format_tests COMMAND archive_format_tests)

add_executable(bcsv_tests tests/data/bcsv_tests.cpp)
target_link_libraries(bcsv_tests PRIVATE smg2_data)
add_test(NAME bcsv_tests COMMAND bcsv_tests)

add_executable(u8_tests tests/data/u8_tests.cpp)
target_link_libraries(u8_tests PRIVATE smg2_data)
add_test(NAME u8_tests COMMAND u8_tests)

add_executable(fst_tests tests/data/fst_tests.cpp)
target_link_libraries(fst_tests PRIVATE smg2_data)
add_test(NAME fst_tests COMMAND fst_tests)

add_executable(j3d_tests tests/data/j3d_tests.cpp)
target_link_libraries(j3d_tests PRIVATE smg2_data)
add_test(NAME j3d_tests COMMAND j3d_tests)
set(SMG2_DATA_ROOT "" CACHE PATH "Local extracted DATA directory for optional game-data checks")
if(SMG2_DATA_ROOT)
    add_test(NAME fst_game_data COMMAND fst_tests "${SMG2_DATA_ROOT}/sys/fst.bin" "${SMG2_DATA_ROOT}/files")
endif()

add_executable(smg2_asset_scan src/tools/asset_scan.cpp)
target_link_libraries(smg2_asset_scan PRIVATE smg2_data)
