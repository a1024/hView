hView

An image viewer that opens:
	.jpg, .png, .gif (still frame), .bmp - using stb_image.h
		https://github.com/nothings/stb/blob/master/stb_image.h
	.webp, .avif, .jp2 - using SAIL
		https://github.com/HappySeaFox/sail
	.heic - using libheif
		https://github.com/strukturag/libheif
	.jxl - using libjxl
		https://gitlab.com/wg1/jpeg-xl
	and .huf files created by RawCam camera2 Android app.

Press F1 for key shortcuts.


Build on Windows:
Make a new project in MSVC 2013 (or later)
Add all sources to project
In project properties, link to the following libraries:
	fftw-3.3.5-dll64				https://fftw.org/download.html
	sail-0.9.0-portable-msvc-2022-x64-release	https://github.com/HappySeaFox/sail/releases
	libheif-1.12.0					https://github.com/strukturag/libheif
