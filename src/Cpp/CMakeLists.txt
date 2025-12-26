if(BUILD_DEBUG_DEMO)
    # 收集当前目录及子目录下的所有源文件和头文件（包括 .h/.hpp，确保IDE显示）
    file(GLOB_RECURSE CURRENT_DIR_SRCS 
        *.cpp 
        *.c 
        *.h 
        *.hpp
    )
    
    # 不需要额外的 EXTRA_DIR_SRCS，因为 GLOB_RECURSE 会递归搜索所有子目录
    set(SOURCES ${CURRENT_DIR_SRCS})

    # 关键：将 SOURCES 传入 add_executable，关联所有文件到目标
    add_executable(ryuni ${SOURCES} version.rc)
    add_executable(debug_demo ALIAS ryuni)

    # 设置头文件包含目录（确保编译器能找到，与IDE显示无关但必须正确）
    target_include_directories(ryuni PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}  # 源目录（影子构建中指向 src 目录）
    )

    if(MSVC)
        target_compile_options(ryuni PRIVATE "/WX-")
        target_compile_options(ryuni PRIVATE /arch:AVX2)
    else()
        target_compile_options(ryuni PRIVATE "-Wno-error")
        target_compile_options(ryuni PRIVATE -mavx2)
    endif()

    target_link_libraries(ryuni MaaCore)

    add_dependencies(ryuni MaaCore)

    set_target_properties(ryuni PROPERTIES WIN32_EXECUTABLE ON ) # 等价于 MSVC 中的“子系统: Windows”
    
    # 为 debug_demo 创建资源目录链接
    create_resource_link(ryuni "$<TARGET_FILE_DIR:debug_demo>")

    # install(TARGETS debug_demo ${MaaCore_install_flatten_args})
endif(BUILD_DEBUG_DEMO)

if(BUILD_SMOKE_TEST)
    add_executable(smoke_test main.cpp)

    if(MSVC)
        target_compile_options(smoke_test PRIVATE "/WX-")
    else()
        target_compile_options(smoke_test PRIVATE "-Wno-error")
    endif()

    target_compile_definitions(smoke_test PRIVATE SMOKE_TESTING)
    target_link_libraries(smoke_test MaaCore)

    add_dependencies(smoke_test MaaCore)
    install(TARGETS smoke_test ${MaaCore_install_flatten_args})
endif(BUILD_SMOKE_TEST)
