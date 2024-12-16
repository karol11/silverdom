add_executable(SDWin WIN32 "sdl2-ogl-skia-simple.cpp")
target_include_directories(SDWin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})    

#target_link_libraries(SDWin PRIVATE ag_alloc)
#target_link_libraries(SDWin PRIVATE user32 gdi32)
target_link_libraries(SDWin PRIVATE OpenGL::GL)
target_link_libraries(SDWin PRIVATE unofficial::skia::skia)
target_link_libraries(SDWin
	PRIVATE
	$<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
	$<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
)

set_property(TARGET SDWin PROPERTY ENABLE_EXPORTS 1)
