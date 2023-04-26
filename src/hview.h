//hview.h - the main hView include header
//Copyright (C) 2022  Ayman Wagih Mohsen
//
//This program is free software: you can redistribute it and/or modify
//it under the terms of the GNU General Public License as published by
//the Free Software Foundation, either version 3 of the License, or
//(at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once
#ifndef HVIEW_H
#define HVIEW_H

//dependencies (can be disabled by commenting-out the macros below)
//FFTW3			https://fftw.org/download.html
//SAIL			https://github.com/HappySeaFox/sail/releases	for WEBP, AVIF & JP2
//LIBHEIF		https://github.com/strukturag/libheif			for HEIF
//LIBJXL		https://gitlab.com/wg1/jpeg-xl					for JXL
//LIBTIFF		https://gitlab.com/libtiff/libtiff				for TIF
//LIBBPG		https://github.com/retsyo/libbpg-py				for BPG
//LIBRAW		https://www.libraw.org/download#stable
//LIBCFITSIO	https://heasarc.gsfc.nasa.gov/fitsio/


	#define		HVIEW_INCLUDE_FFTW
	#define		HVIEW_INCLUDE_SAIL
	#define		HVIEW_INCLUDE_LIBHEIF
	#define		HVIEW_INCLUDE_LIBJXL
	#define		HVIEW_INCLUDE_LIBTIFF
	#define		HVIEW_INCLUDE_LIBBPG
//	#define		HVIEW_INCLUDE_TINYDNGLOADER//superceeded by libraw
	#define		HVIEW_INCLUDE_LIBRAW
	#define		HVIEW_INCLUDE_LIBCFITSIO

//	#define		BENCHMARK


#ifdef HVIEW_INCLUDE_FFTW
#include		"fftw3.h"
#endif
#include		<vector>
#include		<string>
#define _USE_MATH_DEFINES
#include		<math.h>//abs
typedef unsigned char byte;

extern int		w, h, *rgb, rgbn,
				iw, ih;
extern ptrdiff_t image_size;
extern float	*image;//the image
enum			ImageType
{
	IM_UNINITIALIZED,
	IM_GRAYSCALE,
	IM_RGBA,//4 floats per pixel (quad width)	use image_size*4
	IM_BAYER,
	IM_BAYER_SEPARATE,//stored same as bayer, channels are shown separately
};
extern ImageType imagetype;
extern char		bayer[4];//shift ammounts for the 4 Bayer mosaic components, -1 for grayscale
extern int		idepth;

extern double	contrast_gain, contrast_offset;

extern bool		imagecentered;

enum			ProfilePlotMode
{
	PROFILE_OFF,
	PROFILE_X,
	PROFILE_Y,
};
extern ProfilePlotMode profileplotmode;

extern bool		bitmode;
extern int		bitplane;

extern bool		histOn;
extern int		*histogram, histmax_r, histmax_g, histmax_b;//size of 1<<idepth
extern double	invCR[5];

#ifdef FFTW3_H
extern bool		FourierDomain;//if true, show the image in fft_in_planes, otherwise ordinary image
extern int		fft_w, fft_h;//if not equal to image parameters then plans are uninitialized
extern ImageType fft_type;//grayscale: fft_planes[0] only, otherwise use all 4 planes
extern fftw_plan fft_p[4], ifft_p[4];
extern fftw_complex *fft_in_planes[4], *fft_out_planes[4];
#endif

extern double	wpx, wpy,//window position in image coordinates
				zoom,//image pixel size in screen pixels
				invzoom;

extern std::wstring workfolder,//ends with slash
				filetitle;

inline int		minimum(int a, int b){return (a+b-abs(a-b))>>1;}
inline int		maximum(int a, int b){return (a+b+abs(a-b))>>1;}
inline double	minimum(double a, double b){return (a+b-abs(a-b))*0.5;}
inline double	maximum(double a, double b){return (a+b+abs(a-b))*0.5;}

#define			screen2image_x(SX)				(wpx+(SX)*invzoom)
#define			screen2image_y(SY)				(wpy+(SY)*invzoom)
#define			screen2image_x_int(SX)			(int)floor(screen2image_x(SX))
#define			screen2image_y_int(SY)			(int)floor(screen2image_y(SY))
#define			screen2image_x_int_rounded(SX)	(int)floor(screen2image_x(SX)+0.5)
#define			screen2image_y_int_rounded(SY)	(int)floor(screen2image_y(SY)+0.5)
#define			image2screen_x(IX)				((IX-wpx)*zoom)
#define			image2screen_y(IY)				((IY-wpy)*zoom)
#define			image2screen_x_int(IX)			(int)floor(image2screen_x(IX))
#define			image2screen_y_int(IY)			(int)floor(image2screen_y(IY))
/*inline void		screen2image_x(int sx, int &ix)
{
	ix=wpx+(int)floor(sx/zoom);
}
inline void		screen2image_y(int sy, int &iy)
{
	iy=wpy+(int)floor(sy/zoom);
}

inline void		screen2image_x_rounded(int sx, int &ix)
{
	ix=wpx+(int)floor(sx/zoom+0.5);
}
inline void		screen2image_y_rounded(int sy, int &iy)
{
	iy=wpy+(int)floor(sy/zoom+0.5);
}

inline void		screen2image_x(int sx, double &ix)
{
	ix=wpx+sx/zoom;
}
inline void		screen2image_y(int sy, double &iy)
{
	iy=wpy+sy/zoom;
}

inline void		image2screen_x(int ix, int &sx)
{
	sx=int((ix-wpx)*zoom);
}
inline void		image2screen_y(int iy, int &sy)
{
	sy=int((iy-wpy)*zoom);
}//*/

/*inline void		screen2image(int sx, int sy, int &ix, int &iy)
{
	ix=wpx+(int)floor(sx/zoom);
	iy=wpy+(int)floor(sy/zoom);
}
inline void		screen2image_rounded(int sx, int sy, int &ix, int &iy)
{
	ix=wpx+(int)floor(sx/zoom+0.5);
	iy=wpy+(int)floor(sy/zoom+0.5);
}
inline void		screen2image(int sx, int sy, double &ix, double &iy)
{
	ix=wpx+sx/zoom;
	iy=wpy+sy/zoom;
}
inline void		image2screen(int ix, int iy, int &sx, int &sy)
{
	sx=int((ix-wpx)*zoom);
	sy=int((iy-wpy)*zoom);
}//*/
struct			Point2d//for bezier curve
{
	double x, y;
	Point2d():x(0), y(0){}
	Point2d(double x, double y):x(x), y(y){}
	void set(double x, double y){this->x=x, this->y=y;}
	void setzero(){x=y=0;}
	Point2d& operator+=(Point2d const &b){x+=b.x, y+=b.y;return *this;}
	void rotate(double cth, double sth)
	{
		double x2=x*cth+y*sth, y2=-x*sth+y*cth;
		set(x2, y2);
	}
	void transform(double A, double B, double C, double D)
	{
		double x2=x*A+y*B, y2=x*C+y*D;
		set(x2, y2);
	}
	void image2screen()
	{
		x=(x-wpx)*zoom;
		y=(y-wpy)*zoom;
	}
	void screen2image()
	{
		x=wpx+x/zoom;
		y=wpy+y/zoom;
	}
	void clampImage()
	{
		if(x<0)
			x=0;
		if(x>=iw)
			x=iw;
		if(y<0)
			y=0;
		if(y>=ih)
			y=ih;
	}
};
inline Point2d	operator+(Point2d const &a, Point2d const &b){return Point2d(a.x+b.x, a.y+b.y);}
inline Point2d	operator-(Point2d const &a, Point2d const &b){return Point2d(a.x-b.x, a.y-b.y);}
inline Point2d	operator*(double s, Point2d const &b){return Point2d(s*b.x, s*b.y);}
inline bool	operator!=(Point2d const &a, Point2d const &b){return a.x!=b.x||a.y!=b.y;}
inline double abs(Point2d const &a){return sqrt(a.x*a.x+a.y*a.y);}
inline double dot(Point2d const &a, Point2d const &b){return a.x*b.x+a.y*b.y;}
inline bool collinear(Point2d const &a, Point2d const &b, Point2d const &c)
{
	const double tolerance=1e-6;
	return abs((c.x-b.x)*(b.y-a.y)-(b.x-a.x)*(c.y-b.y))<tolerance;
}

//files
long			file_sizew(const wchar_t *filename);
int				open_media();
bool			open_mediaw(const wchar_t *filename);//sets workfolder, updates title
bool			save_media_as();
bool			dialog_get_folder(const wchar_t *user_instr, std::wstring &path);
void			convert_w2utf8(const wchar_t *src, std::string &dst);
bool			get_all_image_filenames(const wchar_t *path, size_t len, std::vector<std::wstring> &filenames);//path ends with slash
void			open_next();
void			open_prev();

//exposed archiver
//int			compress_huff(const short *buffer, int bw, int bh, int depth, int bayer, std::vector<int> &data);
//bool			decompress_huff(const byte *data, int bytesize, RequestedFormat format, void **buffer, int &bw, int &bh, int &depth, char *bayer_sh);
void			print_histogram(int *histogram, int nlevels, ptrdiff_t scanned_size, int *sort_idx, bool CDF=false);
enum			ICER_FilterType
{
	ICER_FILTER_A,//up to 13bit image
	ICER_FILTER_B,// 13bit
	ICER_FILTER_C,//12bit
	ICER_FILTER_D,// 13bit
	ICER_FILTER_E,//12bit
	ICER_FILTER_F,//12bit
	ICER_FILTER_Q,// 13bit
};
const short ICER_filters[]=
{//alphas, beta,	log2(denominator)	up to
	0, 1, 1, 0,		2,//A
	0, 2, 3, 2,		3,//B
	-1, 4, 8, 6,	4,//C
	0, 4, 5, 2,		4,//D
	0, 3, 8, 6,		4,//E
	0, 3, 9, 8,		4,//F
	0, 1, 1, 1,		2,//Q
};
void			ICER_DWT1D(short *buffer, int count, ICER_FilterType filtertype, int nstages=0);
void			ICER_IDWT1D(short *buffer, int count, ICER_FilterType filtertype, int nstages=0);
void			ICER_DWT2D(short *buffer, int bw, int bh, ICER_FilterType filtertype, int nstages=0);
void			ICER_IDWT2D(short *buffer, int bw, int bh, ICER_FilterType filtertype, int nstages=0);
void			encode_zigzag(short *buffer, ptrdiff_t imsize);
void			decode_zigzag(short *buffer, ptrdiff_t imsize);
void			apply_DCT(int logsize, bool inv);
void			archiver_test();
void			archiver_test2();
void			archiver_test3();
void			archiver_test4();
void			stack_images();
void			remove_light_pollution();
void			equalize(int super);
void			archiver_test5();
void			planetary_stabilize();
std::string		filter_path(const char *str, size_t len);//can be same string
std::wstring	filter_path(const wchar_t *str, size_t len);

//image operations
short*			get_image();//delete[] the returned buffer
void			set_image(short *src, int width, int height, int depth, ImageType type);
//void			separate_bayer();
//void			regroup_bayer();
void			debayer();

void			applyFFT();
void			reset_FFTW_state();

void			toggle_histogram();
void			update_histogram();
void			reset_histogram();
void			cmd_histogram();
void			mix_channels();

void			image_transform_fwd();
void			image_transform_inv();

//application
void			center_image();
void			render();
#endif