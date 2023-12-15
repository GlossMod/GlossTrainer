# CMakeLists

# CMake 最低版本号要求
cmake_minimum_required(VERSION 3.26.1)
project(GlossTrainer VERSION 1.0.0 LANGUAGES C CXX)

# 指定为C++17 版本
set(CMAKE_CXX_STANDARD 17)

# 设置生成器平台为32位
set(CMAKE_GENERATOR_PLATFORM Win32)
set(CMAKE_CXX_FLAGS "-m32")

# 配置 vcpkg
set(CMAKE_PREFIX_PATH "E:\\GitHub\\vcpkg\\packages")

include(CTest)
enable_testing()

# 查找目录下的所有源文件
# 并将名称保存到 DIR_SRCS 变量
file(GLOB_RECURSE DIR_SRCS "src/*.cpp" "src/*.h" "src/*.c" "src/*.hpp")

# 引入 include 目录
include_directories(${CMAKE_SOURCE_DIR}/include)

add_library(${PROJECT_NAME} SHARED ${DIR_SRCS})

# 引入 imgui
# find_package(imgui CONFIG REQUIRED)
# target_link_libraries(${PROJECT_NAME} PRIVATE imgui::imgui)

# 引入 minhook
# find_package(minhook CONFIG REQUIRED)

# 链接到你的项目
# target_link_libraries(${PROJECT_NAME} PRIVATE minhook::minhook "d3d11.lib" "dxgi.lib" "dwmapi.lib" "d3dcompiler.lib")
target_link_libraries(${PROJECT_NAME} PRIVATE "d3d11.lib" "dxgi.lib" "dwmapi.lib" "d3dcompiler.lib")

include(CPack)

# 配置导出目录
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "F:\\steam\\steamapps\\common\\Grand Theft Auto IV\\GTAIV\\plugins")

# 配置导出属性
SET_TARGET_PROPERTIES(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE C
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
    OUTPUT_NAME ${PROJECT_NAME}
    PREFIX ""
    SUFFIX ".asi")