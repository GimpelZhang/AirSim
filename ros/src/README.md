# airsim_ros
AirSim ROS packages


A ROS node for the Car SimMode. Usage:

    roslaunch airsim_ros_pkgs airsim_car_node.launch

A keyboard control node for the Car simulated with the ROS node above. Usage:

    roslaunch airsim_car_teleop airsim_car_teleop_key.launch

change log 2020-06-19:

Modified the ros node of car according to: https://github.com/xuhao1/airsim_ros_pkgs

to solve the stereo images timestamps sync problem. 
