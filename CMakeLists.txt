cmake_minimum_required(VERSION 3.5)

project(melloc)
add_executable(melloc src/melloc.cpp 
                      src/arena.cpp
                      src/bin.cpp
                      src/thread_descriptor.cpp
                      src/demo.cpp)
target_include_directories(melloc PUBLIC include)
target_compile_features(melloc PUBLIC cxx_std_20)