cmake_minimum_required(VERSION 2.8.3)
project(rosmsg)
find_package(catkin)
catkin_package()

catkin_python_setup()

if (WIN32)
  add_windows_python_exe_helper(rosmsg)
  add_windows_python_exe_helper(rosmsg-proto)
  add_windows_python_exe_helper(rossrv)
endif()

if(CATKIN_ENABLE_TESTING)
  catkin_add_nosetests(test)
endif()
