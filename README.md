snap_map_icp
----------

snap_map_icp provides ROS node relocalizing a ROS-enabled robot based on it's base laser readings and matching them with the currently advertised /map topic.

Usage:
roslaunch snap_map_icp snap_map_icp.launch

Warning! Make sure you are using the launch file because it does the right topic remaping.

See:
http://ros.org/wiki/SnapMapICP

This is a fork of the discontinued code at:
https://svn.code.sf.net/p/tum-ros-pkg/code/highlevel/SnapMapICP/
