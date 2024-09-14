add_executable(SDWin WIN32 sd-win.cpp)
target_include_directories(SDWin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})    

target_link_libraries(SDWin PRIVATE ag_alloc)
target_link_libraries(SDWin PRIVATE user32 gdi32)
target_link_libraries(SDWin PRIVATE unofficial::skia::skia)
