

SRC =\
 console.c\
 format.c\
 graphics.c\
 hView.c\
 image.c\
 slic2.c\
 window.c\
 util.c\


INCLUDE =\
 -I "C:/Programs/ffmpeg-7.1.1-full_build-shared/include"\
 -I "C:/Projects/a/LibRaw-0.21.4"\
 -I "C:/Projects/a/libheif"\
 -I "E:/Programs/ffmpeg-7.1.1-full_build-shared/include"\
 -I "E:/C/a/LibRaw-0.21.4"\
 -I "E:/C/a/libheif-1.12.0"\


hview:
	gcc -Wall -O3 -s -mavx2 -mlzcnt -mbmi $(INCLUDE) $(SRC) -ohview -lopengl32 -lgdi32 -lole32 -lcomdlg32 -luuid -static
