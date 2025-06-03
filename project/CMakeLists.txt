cmake_minimum_required ( VERSION 3.0.2 )

project ( project )

set(SOIL2_INCLUDE_DIR /home/kalle/libs/SOIL2/src)
set(SOIL2_LIBRARY /home/kalle/libs/SOIL2/build/libsoil2.a)

# Find *all* shaders.
file(GLOB_RECURSE SHADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.frag"
)
# Separate filter for shaders.
source_group("Shaders" FILES ${SHADERS})

# Build and link executable.
add_executable ( ${PROJECT_NAME}
    main.cpp
    ${SHADERS}
    )

target_include_directories( ${PROJECT_NAME} PRIVATE
    ${SOIL2_INCLUDE_DIR}
)

target_link_libraries ( ${PROJECT_NAME} 
    labhelper
    ${SOIL2_LIBRARY}    
)
config_build_output()
