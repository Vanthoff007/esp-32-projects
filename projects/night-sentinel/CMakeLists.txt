cmake_minimum_required(VERSION 3.22)

# Three layers (see README, "Code layout"):
#   platform/  reusable board firmware: camera, SD, Wi-Fi, web server, recorder, inference engine
#   models/    one folder per model; the build picks the one chosen in menuconfig
#   app/       what this project does with the model's results (Night Sentinel)
set(EXTRA_COMPONENT_DIRS platform models app)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(night-sentinel)
