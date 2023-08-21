# CMakeList.txt : CMake project for hView, include source and define project specific logic here.

#add_compile_options(/experimental:c11atomics)
include_directories(
	"C:/Programs/ffmpeg-6.0-full_build-shared/include"#laptop

	"E:/Programs/ffmpeg-6.0-full_build-shared/include"#desktop
	"E:/C/SDL2-2.0.12-MSVC/include"
)
link_directories(
	"C:/Programs/ffmpeg-6.0-full_build-shared/lib"#laptop

	"E:/Programs/ffmpeg-6.0-full_build-shared/lib"#desktop
	"E:/C/SDL2-2.0.12-MSVC/lib/x64"
)
add_executable(hView WIN32
	"console.c"
	"format.c"
	"graphics.c"
	"hView.c"
	"image.c"
	"util.c"
	"window.c"
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
	set_property(TARGET hView PROPERTY C_STANDARD 11)
endif()
