cmake_minimum_required(VERSION 2.8.3)
project(SnapMapICP)

find_package(catkin REQUIRED)

#find_package(catkin)
find_package(Eigen REQUIRED)
include_directories(${EIGEN_INCLUDE_DIRS})
add_definitions(${EIGEN_DEFINITIONS})

add_executable(SnapMapICP src/SnapMapICP.cpp)
add_executable(TFpub src/TFpub.cpp)
add_executable(FreezeCloud2 src/FreezeCloud2.cpp)

target_link_libraries(TFpub FreezeCloud2 SnapMapICP)
