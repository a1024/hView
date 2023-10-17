# CMakeList.txt : CMake project for hView, include source and define project specific logic here.

#add_compile_options(/experimental:c11atomics)
include_directories(
	#includes - laptop
	"C:/Programs/ffmpeg-6.0-full_build-shared/include"
	"C:/Projects/a/LibRaw-0.21.1"
	"C:/Projects/a/libheif"

	#includes - desktop
	"E:/Programs/ffmpeg-6.0-full_build-shared/include"
	"E:/C/SDL2-2.0.12-MSVC/include"
	"E:/C/a/libheif-1.12.0"
	"E:/C/a/LibRaw-0.20.2"
)
link_directories(
	#libraries - laptop
	"C:/Programs/ffmpeg-6.0-full_build-shared/lib"
	"C:/Projects/hView/hView"

	#libraries - desktop
	"E:/Programs/ffmpeg-6.0-full_build-shared/lib"
	"E:/C/SDL2-2.0.12-MSVC/lib/x64"
	"E:/C/hView-cpp/hView"
)
add_executable(hView2 WIN32
	"console.c"
	"format.c"
	"graphics.c"
	"hView.c"
	"image.c"
	"slic2.c"
	"tests.c"
	"util.c"
	"window.c"
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
	set_property(TARGET hView2 PROPERTY C_STANDARD 11)
endif()
