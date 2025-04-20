include_directories(
	#includes - laptop
	#"C:/Programs/ffmpeg-6.0-full_build-shared/include"
	"C:/Projects/a/LibRaw-0.21.4"
	"C:/Projects/a/libheif"

	#includes - desktop
	"E:/Programs/ffmpeg-7.1.1-full_build-shared/include"
	#"E:/Programs/ffmpeg-6.0-full_build-shared/include"
	#"E:/C/SDL2-2.0.12-MSVC/include"
	"E:/C/a/libheif-1.12.0"
	"E:/C/a/LibRaw-0.21.4"
)
#link_directories(
#	#libraries - laptop
#	"C:/Programs/ffmpeg-6.0-full_build-shared/lib"
#	"C:/Projects/hView/hView"
#
#	#libraries - desktop
#	"E:/Programs/ffmpeg-6.0-full_build-shared/lib"
#	#"E:/C/SDL2-2.0.12-MSVC/lib/x64"
#	"E:/C/hView-cpp/hView"
#)
add_executable(hView2 WIN32
	console.c
	format.c
	graphics.c
	hView.c
	image.c
	slic2.c
	util.c
	window.c
)
