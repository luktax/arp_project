cmake_minimum_required(VERSION 3.10)
project(ARP_PROJECT C)

# ------------------------------------------------------------------------------------
# Include directory
# ------------------------------------------------------------------------------------
include_directories(include)

# Output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# ------------------------------------------------------------------------------------
# Libreria comune: process_log
# ------------------------------------------------------------------------------------
add_library(process_log
    src/process_log.c
)

target_include_directories(process_log PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# ------------------------------------------------------------------------------------
# Drone (-lm)
# ------------------------------------------------------------------------------------
add_executable(Drone src/Drone.c)
target_link_libraries(Drone m process_log)

# ------------------------------------------------------------------------------------
# main
# ------------------------------------------------------------------------------------
add_executable(main src/main.c)
target_link_libraries(main process_log)

# ------------------------------------------------------------------------------------
# Blackboard (-lm)
# ------------------------------------------------------------------------------------
add_executable(Blackboard src/Blackboard.c)
target_link_libraries(Blackboard m process_log)

# ------------------------------------------------------------------------------------
# map (-lncursesw)
# ------------------------------------------------------------------------------------
add_executable(map src/map.c)
target_compile_options(map PRIVATE -Wall -Wextra)
target_link_libraries(map ncursesw process_log)

# ------------------------------------------------------------------------------------
# Obstacles
# ------------------------------------------------------------------------------------
add_executable(Obstacles src/Obstacles.c)
target_link_libraries(Obstacles process_log)

# ------------------------------------------------------------------------------------
# Targets
# ------------------------------------------------------------------------------------
add_executable(Targets src/Targets.c)
target_link_libraries(Targets process_log)

# ------------------------------------------------------------------------------------
# I_Keyboard (-lncurses)
# ------------------------------------------------------------------------------------
add_executable(I_Keyboard src/I_Keyboard.c)
target_link_libraries(I_Keyboard ncurses process_log)

# ------------------------------------------------------------------------------------
# Watchdog
# ------------------------------------------------------------------------------------
add_executable(Watchdog src/Watchdog.c)
target_link_libraries(Watchdog process_log)
