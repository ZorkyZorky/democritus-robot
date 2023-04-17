cmake_minimum_required(VERSION 3.5)

 set(EXTRA_COMPONENT_DIRS
     ../../components
     )

#set(EXTRA_COMPONENT_DIRS {IDF_PATH}/components/tinyusb)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(meng2task)
