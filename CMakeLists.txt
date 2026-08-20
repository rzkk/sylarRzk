cmake_minimum_required(VERSION 3.20)
project(mySylar)



#设置全局的c++标准
set (CMAKE_CXX_STANDARD 14) 
set (CMAKE_CXX_STANDARD_REQUIRED True)  # 必须按照 c++11 来

# 强制覆盖型# 设置代码构建级别为 Debug 方式 
# set(CMAKE_BUILD_TYPE Debug)
#以后 可以设置release，  cmake -B build -DCMAKE_BUILD_TYPE=Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

#set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)  # 旧的写法不推荐
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib) #动态库 .so / .dll 输出目录
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib) #静态库 .a / .lib 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin) #可执行文件输出目录

find_package(Threads REQUIRED)

add_subdirectory(src/logger)  # code 依赖于 logger 

# add_subdirectory(logrzk)  # code 依赖于 logger 
# add_executable(loggerTest ${LOGGER_SRCS})
# # target_link_libraries(ReactorV1Test PRIVATE  logger Boost::headers) #${LIBS} # 单个目标 设置依赖库
# # target_include_directories(ReactorV1Test PRIVATE   
# #                      ${CMAKE_CURRENT_SOURCE_DIR}/include
# #                                                     #    ${CMAKE_CURRENT_SOURCE_DIR}/src/pthread
# #                                                  )
# target_compile_options(loggerTest PRIVATE -g -fno-elide-constructors -Wall)

# add_compile_options(-Wall -Wextra -Wpedantic) 