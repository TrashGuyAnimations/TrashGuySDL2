cmake_minimum_required(VERSION 3.10)
project(TrashGuySdl2 LANGUAGES C VERSION 0.0.1)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(tguy main.c)

add_subdirectory(libtguy)
add_subdirectory(vec)
add_subdirectory(utf8c)

if (NOT "${CMAKE_SYSTEM_NAME}" MATCHES "Emscripten")
    find_package(SDL2 REQUIRED)
    find_package(sdl2-ttf REQUIRED)
    find_package(sdl2-image REQUIRED)
    target_link_libraries(tguy PRIVATE SDL2::SDL2-static SDL2::SDL2main SDL2::SDL2_ttf SDL2::SDL2_image)
else ()
    target_link_options(tguy
            PRIVATE
            -fno-rtti -fno-exceptions --closure=1
            -sDISABLE_EXCEPTION_CATCHING=1 -sASSERTIONS=0
            -sEXTRA_EXPORTED_RUNTIME_METHODS=[FS_mkdir,FS_createPreloadedFile]
            -sUSE_SDL=2 -sUSE_SDL_TTF=2 -sUSE_SDL_IMAGE=2
            -sSDL2_IMAGE_FORMATS=[png])

    target_compile_options(tguy
            PRIVATE
            -sUSE_SDL=2 -sUSE_SDL_TTF=2 -sUSE_SDL_IMAGE=2)

    configure_file(templates/tguy.html ${CMAKE_CURRENT_BINARY_DIR}/tguy.html COPYONLY)
    add_custom_command(TARGET tguy PRE_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/templates/tguy.html" "${CMAKE_CURRENT_BINARY_DIR}/")
endif ()

add_custom_command(TARGET tguy PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets" "${CMAKE_CURRENT_BINARY_DIR}/assets")

target_link_libraries(tguy PRIVATE TGuy::TGuy vec utf8c)
