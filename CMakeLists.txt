cmake_minimum_required(VERSION 3.24)
project(IronTide VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(IRONTIDE_BUILD_GAME  "Build the Windows game executable" ${WIN32})
option(IRONTIDE_BUILD_TESTS "Build unit tests" ON)
option(IRONTIDE_BUILD_TOOLS "Build headless tools" ON)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /MP /wd4100 /wd4201)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
  if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
  endif()
endif()

# ---------------------------------------------------------------- third party
add_library(irontide_thirdparty INTERFACE)
target_include_directories(irontide_thirdparty SYSTEM INTERFACE
  ${CMAKE_SOURCE_DIR}/external
  ${CMAKE_SOURCE_DIR}/external/imgui)

add_library(imgui STATIC
  external/imgui/imgui.cpp
  external/imgui/imgui_draw.cpp
  external/imgui/imgui_tables.cpp
  external/imgui/imgui_widgets.cpp
  external/imgui/imgui_demo.cpp)
target_include_directories(imgui SYSTEM PUBLIC external/imgui)
target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
if(NOT MSVC)
  target_compile_options(imgui PRIVATE -w)
endif()

# ---------------------------------------------------------------- portable core
file(GLOB IRONTIDE_CORE_SRC    CONFIGURE_DEPENDS src/core/*.cpp)
file(GLOB IRONTIDE_SIM_SRC     CONFIGURE_DEPENDS src/sim/*.cpp)
file(GLOB IRONTIDE_AI_SRC      CONFIGURE_DEPENDS src/ai/*.cpp)
file(GLOB IRONTIDE_GAME_SRC    CONFIGURE_DEPENDS src/game/*.cpp)
file(GLOB IRONTIDE_PROCGEN_SRC CONFIGURE_DEPENDS src/procgen/*.cpp)
file(GLOB IRONTIDE_UI_SRC      CONFIGURE_DEPENDS src/ui/*.cpp)

add_library(irontide_core STATIC ${IRONTIDE_CORE_SRC})
target_include_directories(irontide_core PUBLIC src)
target_link_libraries(irontide_core PUBLIC irontide_thirdparty)

add_library(irontide_sim STATIC ${IRONTIDE_SIM_SRC})
target_link_libraries(irontide_sim PUBLIC irontide_core)

add_library(irontide_ai STATIC ${IRONTIDE_AI_SRC})
target_link_libraries(irontide_ai PUBLIC irontide_sim)

add_library(irontide_game STATIC ${IRONTIDE_GAME_SRC})
target_link_libraries(irontide_game PUBLIC irontide_ai)

add_library(irontide_procgen STATIC ${IRONTIDE_PROCGEN_SRC})
target_link_libraries(irontide_procgen PUBLIC irontide_sim)

add_library(irontide_ui STATIC ${IRONTIDE_UI_SRC})
target_link_libraries(irontide_ui PUBLIC irontide_game irontide_procgen imgui)

if(NOT WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(irontide_core PUBLIC Threads::Threads)
endif()

# ---------------------------------------------------------------- tests & tools
if(IRONTIDE_BUILD_TESTS)
  enable_testing()
  file(GLOB IRONTIDE_TEST_SRC CONFIGURE_DEPENDS tests/*.cpp)
  add_executable(irontide_tests ${IRONTIDE_TEST_SRC})
  target_link_libraries(irontide_tests PRIVATE irontide_ui)
  target_compile_definitions(irontide_tests PRIVATE IRONTIDE_DATA_DIR="${CMAKE_SOURCE_DIR}/data")
  add_test(NAME irontide_tests COMMAND irontide_tests)
endif()

if(IRONTIDE_BUILD_TOOLS)
  add_executable(irontide_headless tools/headless/main.cpp)
  target_link_libraries(irontide_headless PRIVATE irontide_game)
endif()

# ---------------------------------------------------------------- Windows game
if(IRONTIDE_BUILD_GAME)
  file(GLOB IRONTIDE_RENDER_SRC   CONFIGURE_DEPENDS src/render/*.cpp)
  file(GLOB IRONTIDE_AUDIO_SRC    CONFIGURE_DEPENDS src/audio/*.cpp)
  file(GLOB IRONTIDE_PLATFORM_SRC CONFIGURE_DEPENDS src/platform/win32/*.cpp)
  add_executable(IronTide WIN32
    ${IRONTIDE_RENDER_SRC} ${IRONTIDE_AUDIO_SRC} ${IRONTIDE_PLATFORM_SRC}
    external/imgui/backends/imgui_impl_win32.cpp
    external/imgui/backends/imgui_impl_dx12.cpp)
  target_link_libraries(IronTide PRIVATE irontide_ui d3d12 dxgi dxguid xaudio2_9 ole32 winmm shell32)
  if(MINGW)
    target_link_options(IronTide PRIVATE -static -municode)
  endif()
  if(MSVC)
    set_property(TARGET IronTide PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  endif()
  # Copy data next to the executable so the build output is self-contained.
  add_custom_command(TARGET IronTide POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:IronTide>/data)
endif()
