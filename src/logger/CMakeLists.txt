


# 设置 testserver 可执行文件输出目录
# set_target_properties(testserver PROPERTIES   #properties 属性
#     RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
# )

# add_compile_definitions(PROJECT_ROOT="${CMAKE_SOURCE_DIR}")   #全局的不推荐
# target_compile_definitions(classTest PRIVATE PROJECT_ROOT="${PROJECT_SOURCE_DIR}" )

##########################
# 获取当前目录下的所有源文件
file(GLOB LOGGER_SRCS CONFIGURE_DEPENDS 
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp  # 这个目录是 Cmakelists.txt 所在目录
)



#可执行文件
add_executable(loggerTest ${LOGGER_SRCS})
# target_link_libraries(loggerTest PRIVATE  logger Boost::headers) #${LIBS} # 单个目标 设置依赖库
target_include_directories(loggerTest PRIVATE     
                     ${CMAKE_CURRENT_SOURCE_DIR}/
                                                    #    ${CMAKE_CURRENT_SOURCE_DIR}/src/pthread
                                                 )
target_compile_options(loggerTest PRIVATE -g -fno-elide-constructors -Wall -Wextra -Wpedantic)
# target_link_libraries(loggerTest PRIVATE  logger  ) 
target_link_libraries(loggerTest PUBLIC Threads::Threads)
