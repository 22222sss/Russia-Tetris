cmake_minimum_required(VERSION 3.15)

# set the project name
project(main)

SET(SRC_LIST Tetris.cpp UserInfo.cpp Server.cpp Player.cpp main.cpp)

# add the executable
add_executable(${PROJECT_NAME} ${SRC_LIST})