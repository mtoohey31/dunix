cmake_minimum_required(VERSION 3.28)
file(READ "version.txt" VERSION)
project(dunix VERSION ${VERSION} LANGUAGES CXX)

include(GNUInstallDirs)

add_executable(${PROJECT_NAME} main.cc)
add_definitions(-DVERSION="${VERSION}")

find_package(argparse REQUIRED)

find_package(ftxui REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(NIX REQUIRED nix-main nix-store)

target_link_libraries(${PROJECT_NAME} argparse::argparse ftxui::screen ftxui::dom ftxui::component ${NIX_LIBRARIES})
target_include_directories(${PROJECT_NAME} PUBLIC ${NIX_INCLUDE_DIRS})
target_compile_options(${PROJECT_NAME} PUBLIC ${NIX_CFLAGS_OTHER})

install(TARGETS ${PROJECT_NAME})
