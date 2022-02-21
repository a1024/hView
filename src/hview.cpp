#include		"generic.h"
#include		"hview.h"
#include		<string>
#include		<sstream>
#ifdef FFTW3_H
#pragma			comment(lib, "libfftw3-3.lib")
#endif
static const char file[]=__FILE__;

//dependencies
//FFTW3			https://fftw.org/download.html
//SAIL			https://github.com/HappySeaFox/sail/releases	for WEBP
//LIBHEIF		https://github.com/strukturag/libheif			for HEIF

int				w=0, h=0, *rgb=nullptr, rgbn=0,
				iw=0, ih=0, image_size=0;//image dimensions
float			*image=nullptr;
ImageType		imagetype=IM_RGBA;
char			bayer[4]={};//shift ammounts for the 4 Bayer mosaic components, -1 for grayscale
int				idepth=0;

bool			bitmode=false;//draw standalone bitplane
int				bitplane=0;//see bitmode

bool			histOn=false;
int				*histogram=nullptr, histmax=0;

#ifdef FFTW3_H
bool			FourierDomain=false;
int				fft_w=0, fft_h=0;//if not equal to image parameters then plans are uninitialized
ImageType		fft_type=IM_UNINITIALIZED;//grayscale: fft_planes[0] only, otherwise use all 4 planes
fftw_plan		fft_p[4]={}, ifft_p[4]={};
fftw_complex	*fft_in_planes[4]={}, *fft_out_planes[4]={};
#endif

double			wpx=0, wpy=0,//window position (top-left corner) in image coordinates
//class			Zoom
//{
//	static double val, inv, delta, invdelta;
//	static double get()
//	{
//		return val;
//	}
//	static void set(double _val)
//	{
//		val=_val, inv=1/val;
//	}
//};
				zoom=1,//image pixel size in screen pixels
				invzoom=1,
				zoomdelta=2,
				invzoomdelta=1/zoomdelta;

enum			ZoomMode
{
	AUTOZOOM_OFF,
	AUTOZOOM_ON,
	AUTOZOOM_CENTER,
};
ZoomMode		autozoom=AUTOZOOM_OFF;

double			contrast_gain=1, contrast_offset=0,//gain*(color-offset)+offset
				contrast_delta=1.1;

char			g_buf[g_buf_size]={};
wchar_t			g_wbuf[g_buf_size]={};
const char		default_title[]="hView - Press F1 for shortcut keys";
bool			fullscreen=false;
RECT			oldWindowSize;

int				mx=0, my=0,
				start_mx=0, start_my=0;
enum			Drag
{
	DRAG_NONE,
	DRAG_IMAGE,
};
int				drag=DRAG_NONE;

HINSTANCE		ghInstance=nullptr;
HWND			ghWnd=nullptr;
RECT			R;
HDC				ghDC=nullptr, ghMemDC=nullptr;
HBITMAP			hBitmap=nullptr;
char			kb[256]={};

std::wstring	workfolder,//ends with slash
				filetitle;

bool			mouse_captured=false;
void			mouse_capture()
{
	if(!mouse_captured)
		SetCapture(ghWnd), mouse_captured=true;
}
void			mouse_release()
{
	if(mouse_captured)
		ReleaseCapture(), mouse_captured=false;
}
void			center_image()
{
	int wndw=w, wndh=h-17;
	if((double)wndw/wndh>=(double)iw/ih)//window AR > image AR: fit height
	{
		if(wndh>0)
			zoom=(double)wndh/ih;
	}
	else//window AR < image AR: fit width
		zoom=(double)wndw/iw;
	invzoom=1/zoom;
	wpx=(iw-wndw*invzoom)*0.5;//center image
	wpy=(ih-wndh*invzoom)*0.5;
}
typedef unsigned long long u64;
union			Color16
{
	u64 color;
	unsigned short comp[4];
	Color16():color(0){}
};
inline float	clamp01(float x)
{
	if(x<0)
		return 0;
	if(x>1)
		return 1;
	return x;
}
inline double	clamp01(double x)
{
	if(x<0)
		return 0;
	if(x>1)
		return 1;
	return x;
}
const double
	c_pi2_3=-0.5, s_pi2_3=sqrt(3.)*0.5,
	c_pi4_3=-0.5, s_pi4_3=-s_pi2_3;
inline int		polar2rgb(double mag, double angle)
{
	double ca=cos(angle), sa=sin(angle);
	double
		red		=mag*0.5*(1+ca),
		green	=mag*0.5*(1+ca*c_pi2_3+sa*s_pi2_3),
		blue	=mag*0.5*(1+ca*c_pi4_3+sa*s_pi4_3);

	red		=contrast_gain*(red-contrast_offset)+contrast_offset;
	green	=contrast_gain*(red-contrast_offset)+contrast_offset;
	blue	=contrast_gain*(red-contrast_offset)+contrast_offset;

	red		=clamp01(red);
	green	=clamp01(green);
	blue	=clamp01(blue);

	int color=0;
	auto p=(unsigned char*)&color;
	p[0]=(int)floor(255*blue	+0.5);
	p[1]=(int)floor(255*green	+0.5);
	p[2]=(int)floor(255*red		+0.5);
	return color;
}
void			label_pixels_raw(Point2d const &istart, Point2d const &iend)
{
	if(zoom>=16)
	{
		HFONT hFont=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
		hFont=(HFONT)SelectObject(ghMemDC, hFont);
		int bkMode=0, textColor=0;
		if(zoom<=32)
		{
			textColor=SetTextColor(ghMemDC, 0xFFFFFF);
			bkMode=SetBkMode(ghMemDC, TRANSPARENT);
		}
		int maxlum=(1<<idepth)-1;
		//if(zoom>=64)
		//{
		//	for(int iy=maximum((int)floor(istart.y), 0), yend=minimum((int)floor(iend.y), ih);iy<yend;++iy)
		//	{
		//		int ky=image2screen_y_int(iy);
		//		//int ky=image2screen_y_int(iy+0.5)-8;
		//		for(int ix=maximum((int)floor(istart.x), 0), xend=minimum((int)floor(iend.x), iw);ix<xend;++ix)
		//		{
		//			int kx=image2screen_x_int(ix);
		//			auto lum=image[iw*iy+ix]*maxlum;
		//			GUIPrint(ghMemDC, kx, ky, "%g", lum);
		//		}
		//	}
		//}
		//else
		{
			for(int iy=maximum((int)floor(istart.y), 0), yend=minimum((int)floor(iend.y)+1, ih);iy<yend;++iy)
			{
				int ky=image2screen_y_int(iy);
				//int ky=image2screen_y_int(iy+0.5)-8;
				for(int ix=maximum((int)floor(istart.x), 0), xend=minimum((int)floor(iend.x)+1, iw);ix<xend;++ix)
				{
					int kx=image2screen_x_int(ix);
					auto lum=(int)floor(image[iw*iy+ix]*maxlum+0.5);
					GUIPrint(ghMemDC, kx, ky, "%d", lum);
				}
			}
		}
		if(zoom<=32)
		{
			textColor=SetTextColor(ghMemDC, textColor);
			SetBkMode(ghMemDC, bkMode);
		}
		hFont=(HFONT)SelectObject(ghMemDC, hFont);
	}
}
void			draw_pixel_vectors(Point2d const &istart, Point2d const &iend, int comp, int yoffset, int thickness)//comp: 0 r, 1 g, 2 b, 3 a
{
	int color;
	if(comp==3)
		color=0xFFFFFF;
	else
		color=0xFF<<(comp<<3);
	HPEN hPen=CreatePen(PS_SOLID, 1, color);
	hPen=(HPEN)SelectObject(ghMemDC, hPen);
	//int maxlum=(1<<idepth)-1;
	for(int iy=maximum((int)floor(istart.y), 0), yend=minimum((int)floor(iend.y)+1, ih);iy<yend;++iy)
	{
		int ky=image2screen_y_int(iy);
		for(int ix=maximum((int)floor(istart.x), 0), xend=minimum((int)floor(iend.x)+1, iw);ix<xend;++ix)
		{
			int kx=image2screen_x_int(ix)+1;
			int idx=(iw*iy+ix)<<2;
			int lum=(int)floor(image[idx+comp]*zoom+0.5);
			for(int kt=0;kt<thickness;++kt)
			{
				MoveToEx(ghMemDC, kx, ky+yoffset+kt, 0);
				LineTo(ghMemDC, kx+lum, ky+yoffset+kt);
			}
		}
	}
	hPen=(HPEN)SelectObject(ghMemDC, hPen);
	DeleteObject(hPen);
}
void			label_pixels_rgba(Point2d const &istart, Point2d const &iend)
{
	if(zoom>=64)
	{
		HFONT hFont=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
		hFont=(HFONT)SelectObject(ghMemDC, hFont);
		int bkMode=0;
		if(zoom<=32)
			bkMode=SetBkMode(ghMemDC, TRANSPARENT);
		int maxlum=(1<<idepth)-1;
		for(int iy=maximum((int)floor(istart.y), 0), yend=minimum((int)floor(iend.y)+1, ih);iy<yend;++iy)
		{
			int ky=image2screen_y_int(iy);
			//int ky=image2screen_y_int(iy+0.5)-8;
			for(int ix=maximum((int)floor(istart.x), 0), xend=minimum((int)floor(iend.x)+1, iw);ix<xend;++ix)
			{
				int kx=image2screen_x_int(ix)+3;
				int idx=(iw*iy+ix)<<2;
				int lum_r=(int)floor(image[idx  ]*maxlum+0.5),
					lum_g=(int)floor(image[idx+1]*maxlum+0.5),
					lum_b=(int)floor(image[idx+2]*maxlum+0.5),
					lum_a=(int)floor(image[idx+3]*maxlum+0.5);
				GUIPrint(ghMemDC, kx, ky, "r%d", lum_r);
				GUIPrint(ghMemDC, kx, ky+13, "g%d", lum_g);
				GUIPrint(ghMemDC, kx, ky+26, "b%d", lum_b);
				if(zoom>=128)
					GUIPrint(ghMemDC, kx, ky+39, "a%d", lum_a);
			}
		}
		if(zoom>=64)
		{
			int alphaoffset=13*(zoom>=128);
			draw_pixel_vectors(istart, iend, 0, alphaoffset+40, 2);
			draw_pixel_vectors(istart, iend, 1, alphaoffset+42, 2);
			draw_pixel_vectors(istart, iend, 2, alphaoffset+44, 2);
			//draw_pixel_vectors(istart, iend, 3, alphaoffset+46, 2);
		}
		if(zoom<=32)
			SetBkMode(ghMemDC, bkMode);
		hFont=(HFONT)SelectObject(ghMemDC, hFont);
	}
}
void			copy_pixels(Point2d const &istart, Point2d const &iend)
{
	if(zoom>=16&&!FourierDomain)
	{
		std::stringstream LOL_1;
		int maxlum=(1<<idepth)-1;
		LOL_1<<"Depth: "<<idepth<<" bit\r\n";
		if(imagetype==IM_RGBA)
		{
			for(int iy=maximum((int)floor(istart.y), 0), yend=minimum((int)floor(iend.y)+1, ih);iy<yend;++iy)
			{
				for(int ix=maximum((int)floor(istart.x), 0), xend=minimum((int)floor(iend.x)+1, iw);ix<xend;++ix)
				{
					int idx=(iw*iy+ix)<<2;
					int lum_r=(int)floor(image[idx  ]*maxlum+0.5),
						lum_g=(int)floor(image[idx+1]*maxlum+0.5),
						lum_b=(int)floor(image[idx+2]*maxlum+0.5),
						lum_a=(int)floor(image[idx+3]*maxlum+0.5);
					sprintf_s(g_buf, g_buf_size, "    %4d %4d %4d %4d", lum_r, lum_g, lum_b, lum_a);
					LOL_1<<g_buf;
				}
				LOL_1<<"\r\n";
			}
		}
		else//bayer/grayscale
		{
			for(int iy=maximum((int)floor(istart.y), 0), yend=minimum((int)floor(iend.y)+1, ih);iy<yend;++iy)
			{
				for(int ix=maximum((int)floor(istart.x), 0), xend=minimum((int)floor(iend.x)+1, iw);ix<xend;++ix)
				{
					auto lum=(int)floor(image[iw*iy+ix]*maxlum+0.5);
					sprintf_s(g_buf, g_buf_size, " %4d", lum);
					LOL_1<<g_buf;
				}
				LOL_1<<"\r\n";
			}
		}
		copy_to_clipboard(LOL_1.str());
	}
}
Point2d s_start, s_end, istart, iend;
void			render()
{
	memset(rgb, 0xFF, rgbn<<2);

	if(image)
	{
		s_start.set(0, 0), s_end.set(w, h);
#ifdef FFTW3_H
		bool doubledim=FourierDomain&&imagetype==IM_RGBA;
		if(doubledim)
			iw<<=1, ih<<=1;
#endif
		s_start.screen2image();
		s_end.screen2image();
		s_start.clampImage();
		s_end.clampImage();
		istart=s_start, iend=s_end;
		s_start.image2screen();
		s_end.image2screen();
#ifdef FFTW3_H
		if(doubledim)
			iw>>=1, ih>>=1;
		if(FourierDomain)
		{
			if(imagetype==IM_GRAYSCALE)//1 plane
			{
				for(int ky=maximum((int)floor(s_start.y), 0), yend=minimum((int)floor(s_end.y), h);ky<yend;++ky)
				{
					int iy=screen2image_y_int(ky);
					if(iy<0||iy>=ih)
						continue;
					for(int kx=maximum((int)floor(s_start.x), 0), xend=minimum((int)floor(s_end.x), w);kx<xend;++kx)
					{
						int ix=screen2image_x_int(kx);
						if(ix<0||ix>=iw)
							continue;
						int idx=iw*iy+ix;
						rgb[w*ky+kx]=polar2rgb(fft_in_planes[0][idx][0], fft_in_planes[0][idx][1]);
					}
				}
				//label_pixels_raw(istart, iend);
			}
			else if(imagetype==IM_BAYER)//4 planes shown interleaved
			{
				for(int ky=maximum((int)floor(s_start.y), 0), yend=minimum((int)floor(s_end.y), h);ky<yend;++ky)
				{
					int iy=screen2image_y_int(ky);
					if(iy<0||iy>=ih)
						continue;
					for(int kx=maximum((int)floor(s_start.x), 0), xend=minimum((int)floor(s_end.x), w);kx<xend;++kx)
					{
						int ix=screen2image_x_int(kx);
						if(ix<0||ix>=iw)
							continue;
						int plane=(iy&1)<<1|ix&1, idx=iw*(iy>>1)+(ix>>1);
						rgb[w*ky+kx]=polar2rgb(fft_in_planes[plane][idx][0], fft_in_planes[plane][idx][1]);
					}
				}
			}
			else if(imagetype==IM_BAYER_SEPARATE)//4 planes shown separately at half dimensions
			{
				int w2=iw>>1, h2=ih>>1;
				for(int ky=maximum((int)floor(s_start.y), 0), yend=minimum((int)floor(s_end.y), h);ky<yend;++ky)
				{
					int iy=screen2image_y_int(ky);
					if(iy<0||iy>=ih)
						continue;
					for(int kx=maximum((int)floor(s_start.x), 0), xend=minimum((int)floor(s_end.x), w);kx<xend;++kx)
					{
						int ix=screen2image_x_int(kx);
						if(ix<0||ix>=iw)
							continue;
						int plane=(iy>=h2)<<1|(ix>=w2);
						ix-=w2&-(ix>=w2);
						iy-=h2&-(iy>=h2);
						int idx=iw*iy+ix;
						rgb[w*ky+kx]=polar2rgb(fft_in_planes[plane][idx][0], fft_in_planes[plane][idx][1]);
					}
				}
			}
			else if(imagetype==IM_RGBA)//4 planes shown separately at image dimensions
			{
				int w2=iw<<1, h2=ih<<1;
				for(int ky=maximum((int)floor(s_start.y), 0), yend=minimum((int)floor(s_end.y), h);ky<yend;++ky)
				{
					int iy=screen2image_y_int(ky);
					if(iy<0||iy>=h2)
						continue;
					for(int kx=maximum((int)floor(s_start.x), 0), xend=minimum((int)floor(s_end.x), w);kx<xend;++kx)
					{
						int ix=screen2image_x_int(kx);
						if(ix<0||ix>=w2)
							continue;
						int plane=(iy>=ih)<<1|(ix>=iw);
						ix-=iw&-(ix>=iw);
						iy-=ih&-(iy>=ih);
						int idx=iw*iy+ix;
						rgb[w*ky+kx]=polar2rgb(fft_in_planes[plane][idx][0], fft_in_planes[plane][idx][1]);
					}
				}
			}
		}//end Fourier domain
		else
#endif
		if(bitmode)
		{
			int magitude=(1<<idepth)-1;
			for(int ky=maximum((int)floor(s_start.y), 0), yend=minimum((int)floor(s_end.y), h);ky<yend;++ky)
			{
				int iy=screen2image_y_int(ky);
				if(iy<0||iy>=ih)
					continue;
				for(int kx=maximum((int)floor(s_start.x), 0), xend=minimum((int)floor(s_end.x), w);kx<xend;++kx)
				{
					int ix=screen2image_x_int(kx);
					if(ix<0||ix>=iw)
						continue;
					int lum=(int)floor(magitude*image[iw*iy+ix]+0.5);
					rgb[w*ky+kx]=0xFF000000|-(lum>>bitplane&1);
				}
			}
			label_pixels_raw(istart, iend);
		}
		else if(imagetype==IM_GRAYSCALE)
		{
			for(int ky=maximum((int)floor(s_start.y), 0), yend=minimum((int)floor(s_end.y), h);ky<yend;++ky)
			{
				int iy=screen2image_y_int(ky);
				if(iy<0||iy>=ih)
					continue;
				for(int kx=maximum((int)floor(s_start.x), 0), xend=minimum((int)floor(s_end.x), w);kx<xend;++kx)
				{
					int ix=screen2image_x_int(kx);
					if(ix<0||ix>=iw)
						continue;
					float lum=float(contrast_gain*(image[iw*iy+ix]-contrast_offset)+contrast_offset);
					lum=clamp01(lum);
					int pixel=(int)floor(255*lum+0.5);
					rgb[w*ky+kx]=0xFF000000|pixel<<16|pixel<<8|pixel;
				}
			}
			label_pixels_raw(istart, iend);
		}
		else if(imagetype==IM_BAYER)//raw image
		{
			for(int ky=maximum((int)floor(s_start.y), 0), yend=minimum((int)floor(s_end.y), h);ky<yend;++ky)
			{
				int iy=screen2image_y_int(ky);
				if(iy<0||iy>=ih)
					continue;
				for(int kx=maximum((int)floor(s_start.x), 0), xend=minimum((int)floor(s_end.x), w);kx<xend;++kx)
				{
					int ix=screen2image_x_int(kx);
					if(ix<0||ix>=iw)
						continue;
					float lum=float(contrast_gain*(image[iw*iy+ix]-contrast_offset)+contrast_offset);
					lum=clamp01(lum);
					int pixel=(int)floor(255*lum+0.5);
					//int pixel=int(1023*lum);
					int sh=bayer[(iy&1)<<1|ix&1];
					rgb[w*ky+kx]=0xFF000000|(pixel>>(int)(sh==8))<<sh;//half the greens
					//rgb[w*ky+kx]=0xFF000000|pixel<<16|pixel<<8|pixel;//grayscale
				}
			}
			label_pixels_raw(istart, iend);
		}
		else if(imagetype==IM_BAYER_SEPARATE)//raw image
		{
			int w2=iw>>1, h2=ih>>1;
			for(int ky=maximum((int)floor(s_start.y), 0), yend=minimum((int)floor(s_end.y), h);ky<yend;++ky)
			{
				int iy=screen2image_y_int(ky);
				if(iy<0||iy>=ih)
					continue;
				for(int kx=maximum((int)floor(s_start.x), 0), xend=minimum((int)floor(s_end.x), w);kx<xend;++kx)
				{
					int ix=screen2image_x_int(kx);
					if(ix<0||ix>=iw)
						continue;
					int ix2=(ix-(w2&-(ix>=w2)))<<1|(ix>=w2);
					int iy2=(iy-(h2&-(iy>=h2)))<<1|(iy>=h2);
					float lum=float(contrast_gain*(image[iw*iy2+ix2]-contrast_offset)+contrast_offset);
					lum=clamp01(lum);
					int pixel=(int)(255*lum+0.5);//no filter
					//int pixel=int(1023*lum);
					int sh=bayer[(iy>=h2)<<1|(ix>=w2)];
					rgb[w*ky+kx]=0xFF000000|(pixel>>(int)(sh==8))<<sh;//half the greens
				}
			}
			label_pixels_raw(istart, iend);
		}
		else if(imagetype==IM_RGBA)//ordinary image
		{
			for(int ky=maximum((int)floor(s_start.y), 0), yend=minimum((int)floor(s_end.y), h);ky<yend;++ky)
			{
				int iy=screen2image_y_int(ky);
				if(iy<0||iy>=ih)
					continue;
				for(int kx=maximum((int)floor(s_start.x), 0), xend=minimum((int)floor(s_end.x), w);kx<xend;++kx)
				{
					int ix=screen2image_x_int(kx);
					if(ix<0||ix>=iw)
						continue;
					int idx=(iw*iy+ix)<<2;
					float
						lum_r=float(contrast_gain*(image[idx  ]-contrast_offset)+contrast_offset),
						lum_g=float(contrast_gain*(image[idx+1]-contrast_offset)+contrast_offset),
						lum_b=float(contrast_gain*(image[idx+2]-contrast_offset)+contrast_offset);
					lum_r=clamp01(lum_r);
					lum_g=clamp01(lum_g);
					lum_b=clamp01(lum_b);
					int red		=(int)(255*lum_r+0.5),
						green	=(int)(255*lum_g+0.5),
						blue	=(int)(255*lum_b+0.5);
					int cbcolor=(kx>>4&1)^(ky>>4&1)?0xFFFFFFFF:0xFFEDEDED;
					auto src=(unsigned char*)&cbcolor, dst=(unsigned char*)(rgb+w*ky+kx);
					dst[0]=src[0]+(byte)((blue	-src[0])*image[idx+3]);
					dst[1]=src[1]+(byte)((green	-src[1])*image[idx+3]);
					dst[2]=src[2]+(byte)((red	-src[2])*image[idx+3]);
				}
			}
			label_pixels_rgba(istart, iend);
		}
		if(histOn)//draw histogram
		{
			int nlevels=1<<idepth, wndw=w, wndh=h-17;
			for(int kx=0;kx<w;++kx)
			{
				int idx=kx*(nlevels-1)/(wndw-1);
				int freq=histogram[idx]*wndh/histmax;
				for(int ky=0;ky<freq;++ky)
				{
					//rgb[w*ky+kx]=0xFFFF00FF;
					auto p=(unsigned char*)(rgb+w*ky+kx);
					p[0]+=(0xFF-(int)p[0])>>1;
					p[1]+=(0x00-(int)p[1])>>1;
					p[2]+=(0xFF-(int)p[2])>>1;
				}
			}
		}
	}
	//for(int k=0;k<rgbn;++k)//red screen test
	//	rgb[k]=0xFFFF0000;//
	BitBlt(ghDC, 0, 0, w, h, ghMemDC, 0, 0, SRCCOPY);
}
LRESULT			__stdcall WndProc(HWND hWnd, unsigned message, WPARAM wParam, LPARAM lParam)
{
	switch(message)
	{
	case WM_CREATE:
		break;
	case WM_SIZE:
		{
			GetClientRect(ghWnd, &R);
			int h2=R.bottom-R.top, w2=R.right-R.left;
			if(h!=h2||w!=w2)
			{
				if(!h2)
					h2=1;
				if(autozoom==AUTOZOOM_ON&&w&&h&&w2&&h2)
				{
					zoom*=(double)w2/w;
					//zoom*=0.5*((double)w2/w+(double)h2/h);
					//zoom*=minimum((double)w2/w, (double)h2/h);//X
					invzoom=1/zoom;
				}
				h=h2, w=w2, rgbn=w*h;
				if(autozoom==AUTOZOOM_CENTER&&iw&&ih)
					center_image();
				if(hBitmap)
				{
					hBitmap=(HBITMAP)SelectObject(ghMemDC, hBitmap);
					DeleteObject(hBitmap);
				}
				BITMAPINFO bmpInfo={{sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB, 0, 0, 0, 0, 0}};
				hBitmap=CreateDIBSection(0, &bmpInfo, DIB_RGB_COLORS, (void**)&rgb, 0, 0);
				hBitmap=(HBITMAP)SelectObject(ghMemDC, hBitmap);
			}
		}
		break;
	case WM_MOVE:
		break;
	case WM_PAINT:
		render();
		break;
	case WM_MOUSEMOVE:
		if(drag==DRAG_IMAGE)
		{
			int mx2=(short&)lParam, my2=((short*)&lParam)[1];//client coordinates
			wpx-=(mx2-mx)*invzoom;
			wpy-=(my2-my)*invzoom;
			mx=mx2, my=my2;
			render();
		}
		else
			mx=(short&)lParam, my=((short*)&lParam)[1];//client coordinates
		break;
	case WM_LBUTTONDOWN:
		start_mx=(short&)lParam, start_my=((short*)&lParam)[1];//client coordinates
		drag=DRAG_IMAGE;
		mouse_capture();
		break;
	case WM_LBUTTONUP:
		drag=DRAG_NONE;
		mouse_release();
		break;
	case WM_MOUSEWHEEL:
		{
			bool mw_forward=((short*)&wParam)[1]>0;
			//mx=(short&)lParam, my=((short*)&lParam)[1];//screen coordinates
			double factor, invfactor;
			if(mw_forward)//zoom in
				factor=zoomdelta, invfactor=invzoomdelta;
			else//zoom out
				factor=invzoomdelta, invfactor=zoomdelta;

			wpx+=mx*invzoom*(1-invfactor);
			wpy+=my*invzoom*(1-invfactor);
			zoom*=factor;

			const double tolerance=1e-2;
			if(zoom>1-tolerance&&zoom<1+tolerance)
				zoom=1;
			invzoom=1/zoom;
			render();
		}
		break;
	case WM_KEYDOWN:
		kb[wParam]=true;
		switch(wParam)
		{
		case VK_F1://show key shortcuts
			messageboxa(ghWnd, "Shortcut keys",
				"O: Open file\n"
				"Left/Right: Prev./next file\n"
				"R: 1:1 zoom & reset brightness\n"
				"C: Center image\n"
				"+/-: Adjust brightness\n"
				"B: Split/join Bayer mosaic\n"
				"Ctrl B: Debayer\n"
				"G: Toggle between Bayer and grayscale\n"
				"H: Histogram\n"
				"Ctrl H: Cmd histogram\n"
				"F/F11: Toggle fullscreen\n"
#ifdef FFTW3_H
				"` (grave accent): toggle Fourier transform\n"
#endif
				"1~7: ICER lossless DWTs (grayscale only)\n"
				"Shift 1~7: inverse DWTs\n"
				"Ctrl 1~9: DCT size 2^n\n"
				"Ctrl 0: Inverse DCT\n"
				"\' (quote): Toggle bit mode\n"
				"[: Previous bit plane\n"
				"]: Next bit plane\n"
				"F1: Shortcut keys\n"
				"F2: File properties\n"
				"X: Quit\n"
				"\n"
				"Built on: %s, %s\n",
				__DATE__, __TIME__);
			break;
		case VK_F2://file properties
			{
				long filesize=file_sizew((workfolder+filetitle).c_str());
				long long bitsize=iw*ih*idepth;
				double MBsize=(double)bitsize/(8*1024*1024);
				if(imagetype==IM_BAYER||imagetype==IM_BAYER_SEPARATE)
				{
					char labels[]="BGRA";
					//char labels[]="RGB";
					//int c='B'<<16|'G'<<8|'R';
					messageboxa(ghWnd, "Properties",
						"Width: %d\n"
						"Height: %d\n"
						"Depth: %d bit/channel\n"
						"Bayer mosaic type:\n"
						"\t%c%c\n"
						"\t%c%c\n"
						"File size: %d bytes = %.2lf KB\n"
						"Uncompressed size: %lld bits = %.2lf MB\n",
						iw, ih, idepth,
						labels[bayer[0]>>3], labels[bayer[1]>>3],
						labels[bayer[2]>>3], labels[bayer[3]>>3],
						filesize, (double)filesize/1024,
						bitsize, MBsize);
				}
				else
				{
					messageboxa(ghWnd, "Properties",
						"Width: %d\n"
						"Height: %d\n"
						"Depth: %d bit/channel\n"
						"File size: %d bytes = %.2lf KB\n"
						"Uncompressed size: %lld bits = %.2lf MB\n",
						iw, ih, idepth,
						filesize, (double)filesize/1024,
						bitsize, MBsize);
				}
			}
			break;
		case VK_SPACE:
			archiver_test4();
			//archiver_test3();
			//archiver_test2();
			break;
		case 'O'://open file
			open_media();
			break;
		case VK_LEFT://previous image
			open_prev();
			break;
		case VK_RIGHT://next image
			open_next();
			break;
		case 'R'://reset scale
			wpx=0, wpy=0, zoom=1, invzoom=1;
			contrast_gain=1, contrast_offset=0;
			render();
			break;
		case 'C':
			if(kb[VK_CONTROL])//copy on-screen numbers
			{
				copy_pixels(istart, iend);
				//messageboxa(ghWnd, "Information", "On-screen data was copied to clipboard");
			}
			else//center
			{
				center_image();
				InvalidateRect(ghWnd, nullptr, true);
			}
			break;
		case VK_OEM_7://quote key
#ifdef FFTW3_H
			if(!FourierDomain)
#endif
			if(imagetype==IM_GRAYSCALE)
			{
				bitmode=!bitmode;
				bitplane=0;
				InvalidateRect(ghWnd, nullptr, true);
			}
			break;
		case VK_OEM_4://[ key
			bitplane=(bitplane+1)%idepth;
			InvalidateRect(ghWnd, nullptr, true);
			break;
		case VK_OEM_6://] key
			bitplane=(bitplane-1+idepth)%idepth;
			InvalidateRect(ghWnd, nullptr, true);
			break;
		case VK_OEM_3://tilda	Fourrier transform
			applyFFT();
			InvalidateRect(ghWnd, nullptr, true);
			break;
		case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8':case '9':case '0':
			if(kb[VK_CONTROL])
			{
				static int DCTsize=0;
				if(wParam>'0')
					apply_DCT((int)wParam-'0', false);
				else if(DCTsize)
					apply_DCT(DCTsize, true);
				DCTsize=(int)wParam-'0';
				//static std::wstring DCTfile;
				//static int DCTsize=0;
				//if(wParam=='0')
				//{
				//	if(DCTfile==filetitle&&DCTsize)
				//}
				//else
				//	apply_DCT(wParam-'0', false), DCTsize=wParam-'0';
			}
			else if(imagetype==IM_GRAYSCALE&&wParam<'8')//discrete wavelet transforms
			{
				short *buffer=get_image();
				if(kb[VK_SHIFT])
				{
					decode_zigzag(buffer, image_size);
					ICER_IDWT2D(buffer, iw, ih, (ICER_FilterType)(ICER_FILTER_A+wParam-'1'));
					set_image(buffer, iw, ih, idepth+1, IM_GRAYSCALE);
				}
				else
				{
					ICER_DWT2D(buffer, iw, ih, (ICER_FilterType)(ICER_FILTER_A+wParam-'1'));
					encode_zigzag(buffer, image_size);
					set_image(buffer, iw, ih, idepth-1, IM_GRAYSCALE);
				}
				delete[] buffer;
			}
			render();
			break;
		case VK_OEM_PLUS:case VK_ADD:
			contrast_gain*=contrast_delta;
			render();
			break;
		case VK_OEM_MINUS:case VK_SUBTRACT:
			contrast_gain/=contrast_delta;
			render();
			break;
		case 'B':
			if(kb[VK_CONTROL])//collapse Bayer mosaic
			{
				debayer();
				render();
			}
			else
			{
				if(imagetype==IM_BAYER)//separate Bayer mosaic
				{
					//separate_bayer();
					imagetype=IM_BAYER_SEPARATE;
					render();
				}
				else if(imagetype==IM_BAYER_SEPARATE)//regroup bayer mosaic
				{
					//regroup_bayer();
					imagetype=IM_BAYER;
					render();
				}
			}
			break;
		case 'G':
			if(imagetype==IM_BAYER||imagetype==IM_BAYER_SEPARATE)
				imagetype=IM_GRAYSCALE;
			else if(imagetype==IM_GRAYSCALE)
				imagetype=IM_BAYER;
			render();
			break;
		case 'H'://histogram
			if(kb[VK_CONTROL])
				cmd_histogram();
			else
			{
				toggle_histogram();
				render();
			}
			break;
		case 'S'://stack
			break;
		case 'E'://export
			break;
		case 'F':case VK_F11://fullscreen
			if(fullscreen)//exit fullscreen
			{
				SetWindowLongA(hWnd, GWL_STYLE, WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_CLIPCHILDREN);
				SetWindowPos(hWnd, HWND_TOP, oldWindowSize.left, oldWindowSize.top, oldWindowSize.right-oldWindowSize.left, oldWindowSize.bottom-oldWindowSize.top, SWP_SHOWWINDOW);
			}
			else//enter fullscreen
			{
				GetWindowRect(ghWnd, &oldWindowSize);
				SetWindowLongA(hWnd, GWL_STYLE, WS_OVERLAPPED);
				SetWindowPos(hWnd, HWND_TOP, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), SWP_SHOWWINDOW);
			}
			fullscreen=!fullscreen;
			break;
		case 'X'://quit
			PostQuitMessage(0);
			break;
		}
		break;
	case WM_KEYUP:
		kb[wParam]=false;
		break;
	case WM_CLOSE:
		PostQuitMessage(0);
		break;
	}
	if(image)
	{
		const char *imtypestr="IM_INVALID";
		switch(imagetype)
		{
		case IM_UNINITIALIZED:	imtypestr="IM_UNINITIALIZED";break;
		case IM_GRAYSCALE:		imtypestr="IM_GRAYSCALE";break;
		case IM_RGBA:			imtypestr="IM_RGBA";break;//4 floats per pixel (quad width)
		case IM_BAYER:			imtypestr="IM_BAYER";break;
		case IM_BAYER_SEPARATE:	imtypestr="IM_BAYER_SEPARATE";break;//stored same as bayer, channels are shown separately
		}
		static char bitinfo[100]={};
		if(bitmode)
			sprintf_s(bitinfo, 100, ", bit %d", bitplane);
		int xpos=0, ypos=h-16;
		int imx=screen2image_x_int(mx), imy=screen2image_y_int(my);
		bool mouseinimage=imx>=0&&imx<iw&&imy>=0&&imy<ih;
		Rectangle(ghDC, -1, h-17, w+1, h+1);
		if(mouseinimage)
		{
			int maxlum=(1<<idepth)-1;
			if(imagetype==IM_GRAYSCALE||imagetype==IM_BAYER||imagetype==IM_BAYER_SEPARATE)
			{
				if(imagetype==IM_BAYER_SEPARATE)
				{
					int ix2=imx>=(iw>>1), iy2=imy>=(ih>>1);
					imx-=iw>>1&-ix2;
					imy-=ih>>1&-iy2;
					imx=imx<<1|ix2;
					imy=imy<<1|iy2;
				}
				float lum=image[iw*imy+imx];
				GUIPrint(ghDC, xpos, ypos, "%dx%d, x%g, (%d, %d): Lum=%.6f = %d/%d, contr=%.2lf, %s%s", iw, ih, zoom, imx, imy, lum, (int)(maxlum*lum), maxlum, contrast_gain, imtypestr, bitmode?bitinfo:"");

				//Color16 colorAtMouse;
				//colorAtMouse.color=(u64)(0xFFFF*image[iw*imy+imx])<<(bayer[(imy&1)<<1|imx&1]<<1);
				//GUIPrint(ghDC, xpos, ypos, "%dx%d, %016llX", iw, ih, colorAtMouse.color);
			}
			else if(imagetype==IM_RGBA)
			{
				int idx=(iw*imy+imx)<<2;
				float red=image[idx], green=image[idx+1], blue=image[idx+2], alpha=image[idx+3];
				GUIPrint(ghDC, xpos, ypos, "%dx%d, x%g, (%d, %d): RGBA=(%.4f, %.4f, %.4f, %.4f) = (%d, %d, %d, %d)/%d, contr=%.2lf, %s%s", iw, ih, zoom, imx, imy, red, green, blue, alpha, (int)(maxlum*red), (int)(maxlum*green), (int)(maxlum*blue), (int)(maxlum*alpha), maxlum, contrast_gain, imtypestr, bitmode?bitinfo:"");
			}
			else//unreachable
				GUIPrint(ghDC, xpos, ypos, "%dx%d, x%g, (%d, %d) INVALID STATE, %s%s", iw, ih, zoom, imx, imy, imtypestr, bitmode?bitinfo:"");
		}
		else
			GUIPrint(ghDC, xpos, ypos, "%dx%d, x%g, (%d, %d), %s%s", iw, ih, zoom, imx, imy, imtypestr, bitmode?bitinfo:"");
	}
	return DefWindowProcA(hWnd, message, wParam, lParam);
}
int				__stdcall WinMain(HINSTANCE__ *hInstance, HINSTANCE__ *hPrevInstance, char *lpCmdLine, int nCmdShow)
{
	ghInstance=hInstance;
	tagWNDCLASSEXA wndClassEx=
	{
		sizeof(tagWNDCLASSEXA),
		CS_HREDRAW|CS_VREDRAW|CS_DBLCLKS,
		WndProc, 0, 0, hInstance,

		LoadIconA(nullptr, (char*)IDI_APPLICATION),

		LoadCursorA(nullptr, (char*)IDC_ARROW),//IDC_CROSS
	//	LoadCursorA(nullptr, (char*)0x00007F00),

		nullptr,
	//	(HBRUSH__*)(COLOR_WINDOW+1),
		0, "New format", 0
	};
	short success=RegisterClassExA(&wndClassEx);	SYS_ASSERT(success);
	ghWnd=CreateWindowExA(WS_EX_ACCEPTFILES, wndClassEx.lpszClassName, default_title, WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_CLIPCHILDREN, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);	SYS_ASSERT(ghWnd);//2020-04-20
	
	ShowWindow(ghWnd, nCmdShow);

		//archiver_test();//

		GetClientRect(ghWnd, &R);
		h=R.bottom-R.top, w=R.right-R.left;
		ghDC=GetDC(ghWnd);
		ghMemDC=CreateCompatibleDC(ghDC);
		BITMAPINFO bmpInfo={{sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB, 0, 0, 0, 0, 0}};
		hBitmap=CreateDIBSection(0, &bmpInfo, DIB_RGB_COLORS, (void**)&rgb, 0, 0);
		hBitmap=(HBITMAP)SelectObject(ghMemDC, hBitmap);

		int nArgs;
		wchar_t **args=CommandLineToArgvW(GetCommandLineW(), &nArgs);
		if(nArgs>1)
			open_mediaw(args[1]);
		//else
		//	SetWindowTextA(ghWnd, "hView - Press F1 for key shortcuts");

	tagMSG msg;
	int ret=0;
	for(;ret=GetMessageA(&msg, 0, 0, 0);)
	{
		if(ret==-1)
		{
			LOG_ERROR("GetMessage returned -1 with: hwnd=%08X, msg=%s, wP=%d, lP=%d. \nQuitting.", msg.hwnd, wm2str(msg.message), msg.wParam, msg.lParam);
			break;
		}
		TranslateMessage(&msg);
		DispatchMessageA(&msg);
	}

		DeleteDC(ghMemDC);
		ReleaseDC(ghWnd, ghDC);
		if(image)
			free(image);

	return (int)msg.wParam;
}