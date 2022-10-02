hView

An image viewer that opens:
	.jpg, .png, .gif (still frame), .bmp - using stb_image.h
		https://github.com/nothings/stb/blob/master/stb_image.h
	.dng - using libraw or tiny_dng_loader.h
		https://www.libraw.org
		https://github.com/syoyo/tinydng
	.tif - using libtiff
		https://gitlab.com/libtiff/libtiff
	.webp, .avif, .jp2 - using SAIL
		https://github.com/HappySeaFox/sail
	.bpg - using bpgdec.dll from pubpgviewer
		https://github.com/asimba/pybpgviewer
	.heic - using libheif
		https://github.com/strukturag/libheif
	.jxl - using libjxl
		https://gitlab.com/wg1/jpeg-xl
	.fits - using libcfitsio
		https://heasarc.gsfc.nasa.gov/fitsio/
	and .huf files created by RawCam camera2 Android app.

Press F1 for key shortcuts.


Build on Windows:
Make a new project in MSVC 2013 (or later)
Add all sources from src and third_party to the project
In project properties, link to the following libraries:
	fftw-3.3.5-dll64				https://fftw.org/download.html
	sail-0.9.0-portable-msvc-2022-x64-release	https://github.com/HappySeaFox/sail/releases
	libheif-1.12.0					https://github.com/strukturag/libheif
