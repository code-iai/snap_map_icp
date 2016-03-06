cmake_minimum_required(VERSION 2.8.3)
project(snap_map_icp)

find_package(catkin REQUIRED COMPONENTS
	roscpp
	laser_geometry
	pcl_ros
        pcl_conversions
	tf
        std_msgs
        nav_msgs
	cmake_modules)

find_package(Eigen REQUIRED)
find_package(PCL REQUIRED)

add_definitions(${EIGEN_DEFINITIONS})

include_directories(
  ${catkin_INCLUDE_DIRS}
  ${EIGEN_INCLUDE_DIRS}
  ${PCL_INCLUDE_DIRS})

catkin_package()

add_executable(snap_map_icp src/SnapMapICP.cpp)
add_executable(tf_pub src/TFpub.cpp)
add_executable(freeze_cloud2 src/FreezeCloud2.cpp)

target_link_libraries(tf_pub ${catkin_LIBRARIES})
target_link_libraries(freeze_cloud2 ${catkin_LIBRARIES})
target_link_libraries(snap_map_icp ${catkin_LIBRARIES} ${PCL_LIBRARIES})
