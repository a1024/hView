

SRC =\
 console.c\
 format.c\
 gr2.c\
 graphics.c\
 hView.c\
 image.c\
 metadata.c\
 slic2.c\
 tiles.c\
 window.c\
 util.c\


INCLUDE =\
 -I "C:/Programs/ffmpeg-8.1.1-full_build-shared/include"\
 -I "C:/Projects/a/LibRaw-0.21.4"\
 -I "C:/Projects/a/libheif"\
 -I "E:/Programs/ffmpeg-8.1.1-full_build-shared/include"\
 -I "E:/C/a/LibRaw-0.21.4"\
 -I "E:/C/a/libheif-1.12.0"\


FLAGS =\
 -mavx2\
 -mbmi\
 -mlzcnt\
 -mwindows\
 -static\


LIB =\
 -lcomdlg32\
 -lgdi32\
 -lole32\
 -lopengl32\
 -luuid\
 -lwindowscodecs\


ifeq ($(OS), Windows_NT)
SHELL = cmd.exe
DEL = del
else
DEL = rm
endif

hview:
	g++ -Wall -O3 -mavx2 -c audio.cpp -o audio.o
	gcc -Wall -O3 -s $(FLAGS) $(INCLUDE) $(SRC) audio.o -ohview-gcc $(LIB)
	$(DEL) audio.o
