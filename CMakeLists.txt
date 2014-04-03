cmake_minimum_required(VERSION 2.8.3)
project(SnapMapICP)

find_package(catkin REQUIRED COMPONENTS
	roscpp
	laser_geometry
	pcl_ros
	tf
)

find_package(Eigen REQUIRED)
include_directories(${EIGEN_INCLUDE_DIRS})
add_definitions(${EIGEN_DEFINITIONS})

find_package(PCL REQUIRED)
include_directories(${PCL_INCLUDE_DIRS})

catkin_package()

add_executable(SnapMapICP src/SnapMapICP.cpp)
add_executable(TFpub src/TFpub.cpp)
add_executable(FreezeCloud2 src/FreezeCloud2.cpp)

target_link_libraries(TFpub ${catkin_LIBRARIES})
target_link_libraries(FreezeCloud2 ${catkin_LIBRARIES})
target_link_libraries(SnapMapICP ${catkin_LIBRARIES} ${PCL_LIBRARIES})