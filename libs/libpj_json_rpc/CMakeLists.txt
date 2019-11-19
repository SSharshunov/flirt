cmake_minimum_required(VERSION 2.8 FATAL_ERROR)

set (PROJECT pj_json_rpc) 
project (${PROJECT})

add_library (${PROJECT} INTERFACE)

target_include_directories(${PROJECT} INTERFACE include/)
