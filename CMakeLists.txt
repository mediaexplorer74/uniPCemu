cmake_minimum_required(VERSION 2.8.11)
project(SDL2-image C)

set(SDL2-image-sources SDL_image/IMG.c
                       SDL_image/IMG_bmp.c SDL_image/IMG_gif.c  SDL_image/IMG_jpg.c SDL_image/IMG_lbm.c
                       SDL_image/IMG_pcx.c SDL_image/IMG_png.c  SDL_image/IMG_pnm.c SDL_image/IMG_tga.c
                       SDL_image/IMG_tif.c SDL_image/IMG_webp.c SDL_image/IMG_xcf.c SDL_image/IMG_xpm.c
                       SDL_image/IMG_xv.c  SDL_image/IMG_xxx.c
                       SDL_image/SDL_image.h SDL_image/miniz.h
                       )

if(MSVC)
    list(APPEND SDL2-image-sources SDL_image/version.rc)
endif(MSVC)

add_library(SDL2-image ${SDL2-image-sources})
if(CMAKE_COMPILER_IS_GNUCC)
    set_target_properties(SDL2-image PROPERTIES COMPILE_FLAGS "-fstrict-aliasing -fPIC")
endif()

target_compile_definitions(SDL2-image PRIVATE
    LOAD_TGA
    LOAD_BMP
    LOAD_GIF
    LOAD_XCF
    LOAD_PCX
    LOAD_PNM
    LOAD_XV
    LOAD_PNG
    )
target_link_libraries(SDL2-image LINK_PUBLIC SDL2)
target_include_directories(SDL2-image PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/SDL_image") # TODO: add 'SYSTEM' flag when we have cmake 2.8.12


find_package(PNG QUIET)
if(PNG_FOUND)
    target_link_libraries(SDL2-image LINK_PUBLIC ${PNG_LIBRARIES})
    target_include_directories(SDL2-image PRIVATE ${PNG_INCLUDE_DIRS})

else(PNG_FOUND)
    set(PNG_SHARED OFF CACHE BOOL "Build shared lib." FORCE)
    set(PNG_STATIC ON CACHE BOOL "Build static lib." FORCE)
    add_subdirectory(SDL_image/external/libpng-1.6.2)

    target_link_libraries(SDL2-image LINK_PUBLIC png16_static)
    target_include_directories(SDL2-image PRIVATE SDL_image/external/libpng-1.6.2)
endif(PNG_FOUND)
