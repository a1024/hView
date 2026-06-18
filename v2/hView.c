#define EXPOSE_CVT
#include"hView.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<math.h>
#include<sys/stat.h>
#ifdef _MSC_VER
#include<intrin.h>
#elif defined __GNUC__
#include<x86intrin.h>
#endif
static const char file[]=__FILE__;

//active keys turn on timer
#define ACTIVE_KEY_LIST\
	AK('W') AK('A') AK('S') AK('D')\
	AK(KEY_ENTER) AK(KEY_BKSP)
//	AK(KEY_UP) AK(KEY_DOWN) AK(KEY_LEFT) AK(KEY_RIGHT) AK('T') AK('G')
int active_keys_pressed=0;

typedef enum DragEnum
{
	DRAG_NONE,
	DRAG_IMAGE,
	DRAG_IMAGE_PERSISTENT,
	DRAG_SEEK,
} Drag;
int drag=DRAG_NONE,
	start_mx=0, start_my=0;//initial drag mouse coords

int imagefitted=0;
double zoom=1;//image pixel size in screen pixels
enum
{
	ZOOM_LIMIT_LABEL=48,
	ZOOM_LIMIT_ALPHA=96,
};
double mousewheel_zoom=2;//mouse wheel factor
double wpx=0, wpy=0;//window position (top-left corner) in image coordinates

ArrayHandle fn=0;
Image16 *image=0;
Image8 *impreview=0;
ImageType imagetype=IM_UNINITIALIZED;
int imagedepth=0;
char bayer[4]={0};//indices for the 4 Bayer mosaic components, example: RGGB is {0, 1, 1, 2}
int has_alpha=0;
ptrdiff_t filesize=0;
time_t created=0, lastmodified=0, lastaccess=0;
struct tm datelastmodified={0};
char strlastmodified[128]={0};
unsigned char background[]={0, 0, 0, 255};

static ArrayHandle vertices_text=0;
int pxlabels_hex=1;

int hist_on=0;
int histogram[768];//histogram of preview buffer which is always 8-bit
static ArrayHandle vertices_2d=0;

ProfilePlotMode profileplotmode=PROFILE_OFF;

int bitmode=0,//0: off, 1: monochrome bitplanes, 2: colorful bitplanes
	bitplane=-1;
int pixelpreview=1;
int brightness=0;//impreview = CLAMP(image<<brightness)

extern uint64_t framenumber;
uint32_t image_txid=0;//maintained by image viewer
//uint32_t video_txid=0;//maintained by video playback	X
uint32_t animated=0;
enum
{
	ANIMUI_SLIDER=0,
	ANIMUI_CLEAN,
	ANIMUI_STATS,
	ANIMUI_COUNT,

	SLIDER_HEIGHT=64,

	ANIMATION_NFRAMES=48,
};
int anim_uitype=
#if defined _DEBUG
	ANIMUI_STATS
#else
	ANIMUI_SLIDER
#endif
;
int mute_audio=0;
float g_volume=1;
static int animation_ctr=0;

extern int g_averror, g_avline;
extern char ffmpegerror[64];

int playopt=PLAYOPT_LOOP, playing=0;
double g_timescale=1;
#ifdef PRINT_DEBUGTS
double g_debugts=0;//
#endif

#ifdef PROFILE_FPS
enum
{
	FPS_QUEUESIZE=4096,
	FPS_QUEUEMASK=FPS_QUEUESIZE-1,
};
typedef struct _ProfileEvent
{
	double t;
	ProfileEventType event;
	int start;
} ProfileEvent;
static int fpsprof_colors[EVNT_COUNT]=
{
	0xC00000FF,
	0xC000FF00,
	0xC0FF0000,
	0xC0000000,
};
static int fps_start=0;
static ProfileEvent fps_checkpoints[FPS_QUEUESIZE]={0};
static void *fps_mutex=0;
//double fps_checkpoints[FPS_QUEUESIZE][4]={0};
//double tdec=0, tsend2gpu=0;
void recordevent(int eventtype, int start)
{
	ProfileEvent *e;
	double t;

	if(!fps_mutex)
		fps_mutex=mutex_init();
	t=time_sec();
	mutex_lock(fps_mutex);
	e=fps_checkpoints+fps_start++;
	fps_start&=FPS_QUEUEMASK;
	e->event=eventtype;
	e->start=start;
	e->t=t;
	mutex_unlock(fps_mutex);
}
#endif

int g_rotation=ROTATE_NORMAL;
double g_rotationmatrix[4]={1, 0, 0, 1};
int g_droppedframes=0;
int g_ole32initialized=0;
int g_iw=0, g_ih=0;

int impreview2gpu(uint8_t *data, int iw, int ih, uint32_t *txid)
{
	if(!*txid)
		glGenTextures(1, txid);
	if(!*txid)
	{
		int e=glGetError();
		return e;
	}
	send_texture_pot(*txid, (int*)data, iw, ih, 0, 1);
	return 0;
}
void image_fit2screen(int iw, int ih)
{
	int wndw=w, wndh=h-17;
	double rotw=fabs(g_rotationmatrix[0]*iw)+fabs(g_rotationmatrix[1]*ih);
	double roth=fabs(g_rotationmatrix[2]*iw)+fabs(g_rotationmatrix[3]*ih);
	zoom=fmin((double)wndw/rotw, (double)wndh/roth);
	wpx=(iw-wndw/zoom)*0.5;
	wpy=(ih-wndh/zoom)*0.5;
	//int wndw=w, wndh=h-17;
	//if((double)wndw/wndh>=(double)iw/ih)//window AR > image AR: fit height
	//{
	//	if(wndh>0)
	//		zoom=(double)wndh/ih;
	//}
	//else//window AR < image AR: fit width
	//	zoom=(double)wndw/iw;
	//wpx=(iw-wndw/zoom)*0.5;//center image
	//wpy=(ih-wndh/zoom)*0.5;
	//{
	//	HPoint2D p={0, 0};
	//	screen2image(&p);
	//	wpx=(int)floor(p.x);
	//	wpy=(int)floor(p.y);
	//}
	imagefitted=1;
}
static void calc_hist()
{
	if(!impreview)
		return;
	int res=impreview->iw*impreview->ih;
	memset(histogram, 0, 768*sizeof(int));
	for(int k=0;k<res;++k)
	{
		unsigned char *p=impreview->data+((size_t)k<<2);
		++histogram[0<<8|p[0]];
		++histogram[1<<8|p[1]];
		++histogram[2<<8|p[2]];
	}
}
static void equalize_ch(const unsigned short *src, unsigned char *dst, int iw, int ih, int srcxstride, int srcystride, int *hist, int nlevels, int disableZS)
{
	int mask=nlevels-1;
	if(!(disableZS&1))
		memset(hist, 0, nlevels*sizeof(int));
	for(int ky=0;ky<ih;++ky)
	{
		const unsigned short *srcptr=src+srcystride*ky;
		for(int kx=0;kx<iw;++kx, srcptr+=srcxstride)
			++hist[*srcptr&mask];
	}
	if(!(disableZS&2))
	{
		int total=0;
		for(int ks=0;ks<nlevels;++ks)
			total+=hist[ks];
		int sum=0;
		for(int ks=0;ks<nlevels;++ks)
		{
			int freq=hist[ks];
			hist[ks]=(int)(sum*255LL/total);
			sum+=freq;
		}
		unsigned char *dstptr=dst;
		for(int ky=0;ky<ih;++ky)
		{
			const unsigned short *srcptr=src+srcystride*ky;
			for(int kx=0;kx<iw;++kx, srcptr+=srcxstride, dstptr+=4)
				*dstptr=hist[*srcptr&mask];
		}
	}
}
static void equalize(void)
{
	if(!image||!impreview)
		return;
	int nlevels=1<<imagedepth;
	int *hist=(int*)malloc(nlevels*sizeof(int));
	if(!hist)
	{
		LOG_ERROR("Alloc error");
		return;
	}
	switch(imagetype)
	{
	default://make gcc happy
		break;
	case IM_GRAYSCALEv2:
		equalize_ch(image->data, impreview->data, image->iw, image->ih, 1, image->iw, hist, nlevels, 0);
		for(ptrdiff_t k=0, res=(ptrdiff_t)4*impreview->iw*impreview->ih;k<res;k+=4)//broadcast channel
		{
			impreview->data[k+1]=impreview->data[k];
			impreview->data[k+2]=impreview->data[k];
		}
		break;
	case IM_RGBA:
		equalize_ch(image->data+0, impreview->data+0, image->iw, image->ih, 4, image->iw<<2, hist, nlevels, 0);
		equalize_ch(image->data+1, impreview->data+1, image->iw, image->ih, 4, image->iw<<2, hist, nlevels, 0);
		equalize_ch(image->data+2, impreview->data+2, image->iw, image->ih, 4, image->iw<<2, hist, nlevels, 0);
		break;
	case IM_BAYERv2://FIXME bayer matrix
		for(int kc=0, assigned=0;kc<4;++kc)//green first
		{
			if(bayer[kc]==1)
			{
				equalize_ch(
					image->data+image->iw*(kc>>1)+(kc&1),
					impreview->data+1, image->iw>>1, image->ih>>1,
					2, image->iw*2,
					hist, nlevels, assigned?1:2
				);
				assigned=1;
			}
		}
		for(int kc=0;kc<4;++kc)
		{
			if(bayer[kc]!=1)
			{
				equalize_ch(
					image->data+image->iw*(kc>>1)+(kc&1),
					impreview->data+bayer[kc], image->iw>>1, image->ih>>1,
					2, image->iw*2,
					hist, nlevels, 0
				);
			}
		}
		//equalize_ch((unsigned short*)image->data+image->iw*0+0, impreview->data+0, image->iw>>1, image->ih>>1, 1, image->iw, hist, nlevels, 0);
		//equalize_ch((unsigned short*)image->data+image->iw*0+1, impreview->data+1, image->iw>>1, image->ih>>1, 1, image->iw, hist, nlevels, 2);
		//equalize_ch((unsigned short*)image->data+image->iw*1+0, impreview->data+2, image->iw>>1, image->ih>>1, 1, image->iw, hist, nlevels, 1);
		//equalize_ch((unsigned short*)image->data+image->iw*1+1, impreview->data+3, image->iw>>1, image->ih>>1, 1, image->iw, hist, nlevels, 0);
		break;
	}
	free(hist);
	impreview2gpu(impreview->data, impreview->iw, impreview->ih, &image_txid);
}
static void update_image(int settitle, int render)
{
	if(settitle&&fn)
		set_window_titlew(L"%s - hView", (wchar_t*)fn->data);
	if(!image)
		return;
	if(!impreview||impreview->iw!=image->iw||impreview->ih!=image->ih)
	{
		if(impreview)
			image_free(&impreview);
		switch(imagetype)
		{
		default://make gcc happy
			break;
		case IM_GRAYSCALEv2:
			impreview=image_alloc8(0, image->iw, image->ih, 4, 8);
			break;
		case IM_RGBA:
			impreview=image_alloc8(0, image->iw, image->ih, 4, 8);
			break;
		case IM_BAYERv2:
			impreview=image_alloc8(0, image->iw>>1, image->ih>>1, 4, 8);
			break;
		}
		if(impreview)
		{
			//if(g_rotation==ROTATE_90CW||g_rotation==ROTATE_270CW)
			//{
			//	g_iw=impreview->ih;
			//	g_ih=impreview->iw;
			//}
			//else
			{
				g_iw=impreview->iw;
				g_ih=impreview->ih;
			}
		}
	//	impreview=image_construct(0, 0, 8, 0, image->iw, image->ih, 0, image->depth);
	}
	if(bitmode)
	{
		const unsigned short *src=image->data;
		unsigned char *dst=impreview->data;
		int srcidx, dstidx;
		switch(imagetype)
		{
		default://make gcc happy
			break;
		case IM_GRAYSCALEv2:
			for(int ky=0;ky<image->ih;++ky)
			{
				for(int kx=0;kx<image->iw;++kx)
				{
					srcidx=image->iw*ky+kx, dstidx=(impreview->iw*ky+kx)<<2;
					for(int kc=0;kc<3;++kc)
						dst[dstidx|kc]=src[srcidx]>>bitplane&1?255:0;
				//	dst[dstidx|3]=src[srcidx|3];//copy alpha
					dst[dstidx|3]=255;
				}
			}
			break;
		case IM_RGBA:
			if(bitmode==1)//monochrome bitplanes
			{
				int kb=bitplane%imagedepth;
			//	int kc=bitplane/imagedepth;
				for(int ky=0;ky<image->ih;++ky)
				{
					for(int kx=0;kx<image->iw;++kx)
					{
						srcidx=(image->iw*ky+kx)<<2, dstidx=(impreview->iw*ky+kx)<<2;
						int val=src[srcidx]>>kb&1?255:0;
						dst[dstidx|0]=val;
						dst[dstidx|1]=val;
						dst[dstidx|2]=val;
					//	dst[dstidx|3]=src[srcidx];//copy alpha
						dst[dstidx|3]=255;
					}
				}
			}
			else//colorful bitplanes
			{
				for(int ky=0;ky<image->ih;++ky)
				{
					for(int kx=0;kx<image->iw;++kx)
					{
						srcidx=(image->iw*ky+kx)<<2, dstidx=(impreview->iw*ky+kx)<<2;
						for(int kc=0;kc<3;++kc)
							dst[dstidx|kc]=src[srcidx|kc]>>bitplane&1?255:0;
					//	dst[dstidx|3]=src[srcidx|3]>>8;//copy alpha
						dst[dstidx|3]=255;
					}
				}
			}
			break;
		case IM_BAYERv2:
			{
				int kb=bitplane%imagedepth, kc=bitplane/imagedepth;
				for(int ky=0;ky<impreview->ih;++ky)
				{
					for(int kx=0;kx<impreview->iw;++kx)
					{
						srcidx=image->iw*(ky<<1|(bayer[kc]>>1))+(kx<<1|(bayer[kc]&1));
						dstidx=(impreview->iw*ky+kx)<<2;
						int bit=src[srcidx]>>kb&1?255:0;
						dst[dstidx|0]=bit;
						dst[dstidx|1]=bit;
						dst[dstidx|2]=bit;
					//	dst[dstidx|3]=src[srcidx|3];//copy alpha
						dst[dstidx|3]=255;
					}
				}
			}
			break;
		}
	}
	else
		image_export(impreview, image, imagetype);
	//	image_export_rgb8(impreview, image, imagetype);
	//	image_blit(impreview, 0, 0, image->data, image->iw, image->ih, image->xcap-image->iw, image->depth);
	if(hist_on)
		calc_hist();
	if(imagefitted)
		image_fit2screen(g_iw, g_ih);
	impreview2gpu(impreview->data, impreview->iw, impreview->ih, &image_txid);
	if(render)
		io_render();
}
static int bitplane_mreduce(int bitplane)
{
	switch(imagetype)
	{
	default://make gcc happy
		break;
	case IM_GRAYSCALEv2:
		if(bitmode)
			MODVAR(bitplane, bitplane, imagedepth);
		break;
	case IM_RGBA:
		if(bitmode==2)
			MODVAR(bitplane, bitplane, imagedepth);
		else if(bitmode==1)
		{
			int n=imagedepth*3;
			MODVAR(bitplane, bitplane, n);
		}
		break;
	case IM_BAYERv2:
		if(bitmode)
		{
			int n=imagedepth*4;
			MODVAR(bitplane, bitplane, n);
		}
		break;
	}
	return bitplane;
}
static void zoom_at(int xs, int ys, double factor)
{
	const double tolerance=1e-2;
	wpx+=xs/zoom*(1-1/factor);
	wpy+=ys/zoom*(1-1/factor);
	zoom*=factor;
	if(fabs(zoom-1)<tolerance)
		zoom=1;

	imagefitted=0;
}
void playbackendaction(void)
{
	if(playopt==PLAYOPT_LIST||playopt==PLAYOPT_SHUF)
	{
		ArrayHandle path=0, filenames=0, *fn2=0;

		videoplayback_pause(1);
		srand((uint32_t)__rdtsc());
		WSTR_COPY(path, fn->data, fn->count);
		wchar_t *str=(wchar_t*)path->data;
		for(int k=(int)path->count-1;k>=0;--k)
		{
			if(str[k]==L'/'||str[k]==L'\\')
			{
				str[k+1]=0;
				path->count=k+1LL;
				break;
			}
		}
		filenames=get_filenamesw(str, 0, 0, 1);
		array_free(&path);
		if(filenames&&filenames->count)
		{
			Image16 *im2=0;
			if(playopt==PLAYOPT_SHUF)
			{
				for(int it=0;it<(int)filenames->count;++it)
				{
					int idx=rand()<<15^rand();
					idx%=(int)filenames->count;
					fn2=(ArrayHandle*)array_at(&filenames, idx);
					if(!load_media((wchar_t*)fn2[0]->data, &im2, 0, 1))
						break;
					if(im2)
						image_free(&im2);
				}
			}
			else
			{
				int currentidx=-1;
				for(int k=0;k<(int)filenames->count;++k)
				{
					fn2=(ArrayHandle*)array_at(&filenames, k);
					if(!wcscmp((wchar_t*)fn2[0]->data, (wchar_t*)fn->data))
					{
						currentidx=k;
						break;
					}
				}
				if(currentidx==-1)
#if defined _DEBUG &&0
				{
					console_start();
					for(int k=0;k<(int)filenames->count;++k)
					{
						fn2=(ArrayHandle*)array_at(&filenames, k);
						console_logw(L"%s\n", (wchar_t*)fn2[0]->data);
					}
					console_logw(L"\n%s\n", (wchar_t*)fn->data);
					LOG_WARNING("Navigation error");
				}
#else
				{
					LOG_WARNING("Navigation error");
					array_free(&filenames);
					return;
				}
#endif
				int step=1;
				for(int k=currentidx+step;MODVAR(k, k, (int)filenames->count), k!=currentidx;k+=step)
				{
					fn2=(ArrayHandle*)array_at(&filenames, k);
					if(!load_media((wchar_t*)fn2[0]->data, &im2, 0, 1))
					{
						currentidx=k;
						break;
					}
					if(im2)
						image_free(&im2);
				}
			}
			if((im2||animated)&&fn2)//fn2: to please dumb MSVC linter
			{
				image_free(&image);
				image_free(&impreview);
				image=im2;
				array_free(&fn);
				fn=filter_pathw((wchar_t*)fn2[0]->data, -1, 0);
				update_image(1, 1);
			}
			array_free(&filenames);
		}
	}
}
int io_init(int argc, wchar_t **argv)//return false to abort
{
#if defined _DEBUG ||0
//#if 0
	const wchar_t *filename=

		L"C:/Users/Work Pc/Documents/Half-Life_2041_720p120.mkv"
	//	L"D:/ML/dataset-Internet/os_bleh.gif"
	//	L"E:/Share Box/Sound & Music/20250411 2.mp3"
	//	L"E:/Share Box/Sound & Music/2024-11-07 at 3.54.45 PM.mp4"
	//	L"E:/Share Box/Sound & Music/145 (Poodles) by Jake Chudnow [DokBeZKKeKI].opus"
	//	L"D:/ML/dataset-Internet/quantum.mp4"
	//	L"D:/ML/dataset-Internet/birds.webm"
	//	L"D:/ML/dataset-Internet/star_wars.webm"
	//	L"D:/ML/dataset-Internet/accident.webm"	//silent
	//	L"D:/ML/dataset-Internet/revolver.webm"
	//	L"D:/ML/dataset-Internet/briefcase.webm"
	//	L"D:/ML/dataset-Internet/sharks.webm"
	//	L"C:/dataset-20241107-gr/20241107_164228_958.gr"
	//	L"D:/ML/dataset-20250416-raw/P1000058.RW2"
	//	L"D:/ML/WhatsApp Stickers/STK-20230621-WA0009.webp"
	//	L"E:/Share Box/20230102 dslr/NEF/DSC_0403.NEF"
	//	L"D:/ML/20250320_005230.dng"
	//	L"D:/ML/dataset-RAW/a0001-jmac_DSC1459.dng"
	//	L"D:/ML/dataset-RAW/a0117-kme_006.dng"
	//	L"D:/ML/dataset-RAW/a0118-20051223_103622__MG_0617.dng"
	//	L"D:/ML/dataset-RAW/a0128-IMG_0793.dng"
	//	L"D:/ML/00-Taschdid.svg.png"
	//	L"D:/ML/mystery.gr"
	//	L"D:/ML/kodim24.ppm"
	//	L"D:/ML/mystery.gr"
	//	L"E:/C/ASCII-Table-wide.svg.png"
	//	L"C:/dataset-20241107-gr/20241107_164228_958.gr"
	//	L"E:/C/huf2gr/huf2gr/plane.gr"
	//	L"E:/Share Box/Scope/20241107/20241107_164651_573.huf"
	//	L"E:/Share Box/Scope/20241107/20241107_164228_958.huf"
	//	L"C:/Projects/datasets/dataset-RAW/6K9A8788.CR3"

	;
	load_media(filename, &image, 1, 0);
	if(image||animated)
	{
		fn=filter_pathw(filename, -1, 0);
		update_image(1, 0);
		if(impreview)
			image_fit2screen(g_iw, g_ih);//
	}
	else
		array_free(&fn);
#else
	if(argc>1)
	{
		fn=filter_pathw(argv[1], -1, 0);
		load_media((wchar_t*)fn->data, &image, 1, 0);
		if(image||animated)
		{
			update_image(1, 0);
			if(impreview)
				image_fit2screen(g_iw, g_ih);//
		}
		else
			array_free(&fn);
	}
#endif
	if(!image&&!animated)
		set_window_titlew(L"hView");
	return 1;
}
void io_dropfile(const wchar_t *filename)
{
	Image16 *im2=0;

	load_media(filename, &im2, 0, 0);
	if(im2||animated)
	{
		image_free(&image);
		image=im2;
		array_free(&fn);
		fn=filter_pathw(filename, -1, 0);
		update_image(1, 1);
	}
}
void io_resize()
{
	if(impreview&&impreview->iw&&impreview->ih&&imagefitted)
		image_fit2screen(g_iw, g_ih);
}
int io_mousemove()//return true to redraw
{
	if(drag==DRAG_IMAGE)
	{
		wpx-=(mx-(w>>1))/zoom;
		wpy-=(my-(h>>1))/zoom;
		//int X0=w>>1, Y0=h>>1;
		//cam_turnMouse(cam, mx-X0, my-Y0, mouse_sensitivity);
		set_mouse(w>>1, h>>1);
		//return !timer;
	}
	else if(drag==DRAG_SEEK)
		slider_set((double)mx/w, 1);
	return !timer&&!playing;
}
int io_mousewheel(int forward)
{
	int mw_fwd=forward>0;
	if(animated)
	{
		if(anim_uitype!=ANIMUI_CLEAN&&(uint32_t)(my-(h-tdy-SLIDER_HEIGHT))<SLIDER_HEIGHT)//change animation timescale
			slider_changespeed(mw_fwd?1.1:1./1.1);
		else
		{
			mute_audio=0;
			g_volume*=mw_fwd?2:0.5f;
			if(mw_fwd&&!g_volume)
				g_volume=1.f/128;
			if(g_volume>2)
				g_volume=2;
		}
		animation_ctr=ANIMATION_NFRAMES;
	}
	else if(GET_KEY_STATE(KEY_CTRL))
	{
		brightness+=2*mw_fwd-1;
		update_image(0, 0);
	}
	else
		zoom_at(mx, my, mw_fwd?mousewheel_zoom:1/mousewheel_zoom);//fwd zooms in
	return 1;
}
static void count_active_keys(IOKey upkey)
{
	keyboard[upkey]=0;
	active_keys_pressed=0;
#define		AK(KEY)		active_keys_pressed+=keyboard[KEY];
	ACTIVE_KEY_LIST
#undef		AK
	if(!active_keys_pressed)
		timer_stop(TIMER_ID_KEYBOARD);
}
int io_keydn(IOKey key, char c)
{
	if(GET_KEY_STATE(KEY_CTRL))//timer keys handled outside the switch block
	{
		switch(key)
		{
		default://make gcc happy
			break;
		case 'A'://set alpha to one
			if(imagetype==IM_RGBA)
			{
				uint16_t *data=image->data;
				ptrdiff_t res=image->iw*image->ih;
				for(ptrdiff_t k=0;k<res;++k)
					data[k<<2|3]=0xFFFF;
				for(ptrdiff_t k=0;k<res;++k)
					impreview->data[k<<2|3]=0xFF;
				return 1;
			}
			return 0;
		case 'S'://save as
			{
				int kslash=0, kdot=0;
				wchar_t *str=(wchar_t*)fn->data;
				for(kdot=(int)fn->count-1;kdot>=0&&str[kdot]!=L'.';--kdot);
				kslash=kdot-1;
				for(kslash=kdot-1;kslash>=0&&str[kslash]!=L'/'&&str[kslash]!=L'\\';--kslash);
				++kslash;
				save_media_as(image, impreview, str+kslash, kdot-kslash, 1);
			}
			return 1;
		}
	}
	switch(key)
	{
	default://make gcc happy
		break;
	case KEY_F1:
		{
			char *ver=get_libinfo();
			messageboxa(MBOX_OK, "Controls",
				"Esc/LBUTTON/WASD: Drag image\n"
				"Enter/Bksp/Wheel: Zoom image\n"
				"E: Reset view to topleft corner at 1:1\n"
				"C: Fit image to window\n"
				"+/-: Change HDR brightness\n"
				"F: Hide video slider\n"
				"Q: Equalize histogram\n"
				"G: Reinterpret Bayer image as grayscale and vice versa\n"
				"Left/Right: Prev/next image\n"
				"Ctrl O: Open image\n"
				"R/F5: Refresh\n"
				"Ctrl X/Y/R/T: Flip/rotate/transpose\n"
				"Ctrl S: Save as\n"
				"Ctrl A: Set alpha to 1\n"
				"Ctrl C: Crop-copy pixels/values\n"
				"Ctrl V: Paste bitmap from clipboard\n"
				"H: Toggle histogram\n"
				"Ctrl H: Toggle hex/decimal pixel labels\n"
				"P: Toggle pixel label source / Playback end action\n"
				"X/Y: Toggle horizontal/vertical cross-sections\n"
				"Quote: Toggle bitplane view\n"
				"Brackets: Select bitplane\n"
				"F1: Controls\n"
				"F2: Metadata\n"
				"\n"
				"%s\n"
				"%s %s\n"
				, ver?ver:""
				, __DATE__, __TIME__
			);
			free(ver);
		}
		break;
	case KEY_F2://info
		{
			wchar_t buf[2048]={0};
			if(image&&impreview)
			{
				int nprinted=0;
				ptrdiff_t usize=0;
				time_t created2=0, lastmodified2=0, lastaccess2=0;
				switch(imagetype)
				{
				default://make gcc happy
					break;
				case IM_GRAYSCALEv2:
				case IM_RGBA:
					usize=(ptrdiff_t)ceil((double)image->srcnch*image->iw*image->ih*log2(image->nlevels0)/8);
					break;
				case IM_BAYERv2:
					usize=(ptrdiff_t)ceil((double)image->iw*image->ih*log2(image->nlevels0)/8);
					break;

				}
				int extidx=0;
				if(fn)
				{
					struct _stat64 info={0};
					int e2=_wstat64((wchar_t*)fn->data, &info);
					if(e2)
						nprinted+=_snwprintf_s(buf+nprinted, _countof(buf)-1-nprinted, _countof(buf)-1-nprinted,
							L"\"%s\"\n"
							L"INACCESSIBLE\n"
							, (wchar_t*)fn->data
						);
					else
					{
						ptrdiff_t csize=info.st_size;
						created2=info.st_ctime;
						lastmodified2=info.st_mtime;
						lastaccess2=info.st_atime;
						get_filetitlew((wchar_t*)fn->data, (int)fn->count, 0, &extidx);
						nprinted+=_snwprintf_s(buf+nprinted, _countof(buf)-1-nprinted, _countof(buf)-1-nprinted,
							L"\"%s\"\n"
							L"%11td bytes ("
							, (wchar_t*)fn->data
							, usize
						);
						nprinted+=print_memsizew(buf+nprinted, _countof(buf)-1-nprinted, usize, 8);
						nprinted+=_snwprintf_s(buf+nprinted, _countof(buf)-1-nprinted, _countof(buf)-1-nprinted,
							L") bitmap\n"
							L"%11td bytes ("
							, csize
						);
						nprinted+=print_memsizew(buf+nprinted, _countof(buf)-1-nprinted, csize, 8);
						nprinted+=_snwprintf_s(buf+nprinted, _countof(buf)-1-nprinted, _countof(buf)-1-nprinted,
							L") %s\n"
							, (wchar_t*)fn->data+extidx
						);
					}
				}
				else
				{
					nprinted+=_snwprintf_s(buf+nprinted, _countof(buf)-1-nprinted, _countof(buf)-1-nprinted,
						L"%10td bytes ("
						, usize
					);
					nprinted+=print_memsizew(buf+nprinted, _countof(buf)-1-nprinted, usize, 8);
					nprinted+=_snwprintf_s(buf+nprinted, _countof(buf)-1-nprinted, _countof(buf)-1-nprinted, L") bitmap\n");
				}
				nprinted+=_snwprintf_s(buf+nprinted, _countof(buf)-1-nprinted, _countof(buf)-1-nprinted, L"\n");
				switch(imagetype)
				{
				default://make gcc happy
					break;
				case IM_GRAYSCALEv2:
					nprinted+=_snwprintf_s(buf+nprinted, _countof(buf)-1-nprinted, _countof(buf)-1-nprinted,
						L"CWH %d*%5d*%5d*log2(%5d) G\n"
						, image->srcnch, image->iw, image->ih, image->nlevels0
					);
					break;
				case IM_RGBA:
					if(image->srcnch>=3)
						nprinted+=_snwprintf_s(buf+nprinted, _countof(buf)-1-nprinted, _countof(buf)-1-nprinted,
							L"CWH %d*%5d*%5d*log2(%5d) RGB%s\n"
							, image->srcnch, image->iw, image->ih, image->nlevels0, image->srcnch==4?L"A":L""
						);
					else
						nprinted+=_snwprintf_s(buf+nprinted, _countof(buf)-1-nprinted, _countof(buf)-1-nprinted,
							L"CWH %d*%5d*%5d*log2(%5d) G%s\n"
							, image->srcnch, image->iw, image->ih, image->nlevels0, image->srcnch==2?L"A":L""
						);
					break;
				case IM_BAYERv2:
					{
						char labels[]="RGB";
						nprinted+=_snwprintf_s(buf+nprinted, _countof(buf)-1-nprinted, _countof(buf)-1-nprinted,
							L" WHD  %5d*%5d*log2(%5d) %c%c%c%c\n"
							, image->iw, image->ih, image->nlevels0
							, labels[(int)bayer[0]]
							, labels[(int)bayer[1]]
							, labels[(int)bayer[2]]
							, labels[(int)bayer[3]]
						);
					}
					break;
				}
				nprinted+=_snwprintf_s(buf+nprinted, _countof(buf)-1-nprinted, _countof(buf)-1-nprinted,
					L"CWH %d*%5d*%5d*8 preview\n"
					, impreview->nch, g_iw, g_ih
				);
				if(created2)
				{
					struct tm date={0};
					nprinted+=_snwprintf_s(buf+nprinted, _countof(buf)-1-nprinted, _countof(buf)-1-nprinted, L"\n");
					localtime_s(&date, &created2);
					nprinted+=(int)wcsftime(buf+nprinted, _countof(buf)-1-nprinted, L"%Y-%m-%d_%H%M%S Created\n", &date);
					localtime_s(&date, &lastmodified2);
					nprinted+=(int)wcsftime(buf+nprinted, _countof(buf)-1-nprinted, L"%Y-%m-%d_%H%M%S Modified\n", &date);
					localtime_s(&date, &lastaccess2);
					nprinted+=(int)wcsftime(buf+nprinted, _countof(buf)-1-nprinted, L"%Y-%m-%d_%H%M%S Accessed\n", &date);
				}

				int cancel=messageboxw(MBOX_OKCANCEL, L"Copy to clipboard?", L"%s", buf);
				if(!cancel)
					copy_to_clipboardw(buf, nprinted);
			}
			else if(fn)
			{
				struct _stat64 info={0};
				struct tm date={0};
				int nprinted=0;

				int e2=_wstat64((wchar_t*)fn->data, &info);

				if(e2)
					nprinted+=_snwprintf_s(buf+nprinted, _countof(buf)-1-nprinted, _countof(buf)-1-nprinted,
						L"\"%s\"\n"
						L"INACCESSIBLE\n"
						, (wchar_t*)fn->data
					);
				else
				{
					nprinted+=_snwprintf_s(buf+nprinted, _countof(buf)-1-nprinted, _countof(buf)-1-nprinted,
						L"\"%s\"\n"
						L"%11lld bytes ("
						, (wchar_t*)fn->data
						, (int64_t)info.st_size
					);
					nprinted+=print_memsizew(buf+nprinted, _countof(buf)-1-nprinted, info.st_size, 8);
					nprinted+=_snwprintf_s(buf+nprinted, _countof(buf)-1-nprinted, _countof(buf)-1-nprinted, L")\n");

					nprinted+=_snwprintf_s(buf+nprinted, _countof(buf)-1-nprinted, _countof(buf)-1-nprinted, L"\n");
					localtime_s(&date, &info.st_ctime);
					nprinted+=(int)wcsftime(buf+nprinted, _countof(buf)-1-nprinted, L"%Y-%m-%d_%H%M%S Created\n", &date);
					localtime_s(&date, &info.st_mtime);
					nprinted+=(int)wcsftime(buf+nprinted, _countof(buf)-1-nprinted, L"%Y-%m-%d_%H%M%S Modified\n", &date);
					localtime_s(&date, &info.st_atime);
					nprinted+=(int)wcsftime(buf+nprinted, _countof(buf)-1-nprinted, L"%Y-%m-%d_%H%M%S Accessed\n", &date);
				}
				int cancel=messageboxw(MBOX_OKCANCEL, L"Copy to clipboard?", L"%s", buf);
				if(!cancel)
					copy_to_clipboardw(buf, nprinted);
			}
		}
		break;
	case KEY_ESC:
	case KEY_LBUTTON:
		if(animated&&anim_uitype!=ANIMUI_CLEAN&&key==KEY_LBUTTON&&(uint32_t)(my-(h-tdy-SLIDER_HEIGHT))<SLIDER_HEIGHT)
		{
			drag=DRAG_SEEK;
			slider_set((double)mx/w, 0);
			mouse_capture();
		}
		else if(drag==DRAG_NONE)//start dragging
		{
			drag=GET_KEY_STATE(KEY_CTRL)?DRAG_IMAGE_PERSISTENT:DRAG_IMAGE;//ctrl LBUTTON: persistent drag
			start_mx=mx, start_my=my;
			show_mouse(0);
			set_mouse(w>>1, h>>1);
			mouse_capture();
		}
		else if(key==KEY_ESC||drag==DRAG_IMAGE_PERSISTENT)//stop dragging
		{
			drag=DRAG_NONE;
			mouse_release();
			set_mouse(start_mx, start_my);
			show_mouse(1);
		}
		break;

#define		AK(KEY)		case KEY:
	ACTIVE_KEY_LIST
#undef		AK
		timer_start(10, TIMER_ID_KEYBOARD);
		break;
		
	case KEY_PLUS:
	case KEY_MINUS:
	case KEY_NP_PLUS:
	case KEY_NP_MINUS:
		brightness+=2*(key==KEY_PLUS||key==KEY_NP_PLUS)-1;
		update_image(0, 0);
		return 1;
	case KEY_LEFT:
	case KEY_RIGHT:
		if(fn)
		{
			ArrayHandle path;
			WSTR_COPY(path, fn->data, fn->count);
			//acme_strrchr((char*)path->data, path->count, '/');//X  what about backslash?
			wchar_t *str=(wchar_t*)path->data;
			for(int k=(int)path->count-1;k>=0;--k)
			{
				if(str[k]==L'/'||str[k]==L'\\')
				{
					str[k+1]=0;
					path->count=k+1;
					break;
				}
			}
			ArrayHandle filenames=get_filenamesw(str, 0, 0, 1), *fn2=0;
			array_free(&path);
			if(filenames&&filenames->count)
			{
				int currentidx=-1;
				for(int k=0;k<(int)filenames->count;++k)
				{
					fn2=(ArrayHandle*)array_at(&filenames, k);
					if(!wcscmp((wchar_t*)fn2[0]->data, (wchar_t*)fn->data))
					{
						currentidx=k;
						break;
					}
				}
				if(currentidx==-1)
#ifdef _DEBUG
				{
					console_start();
					for(int k=0;k<(int)filenames->count;++k)
					{
						fn2=(ArrayHandle*)array_at(&filenames, k);
						console_logw(L"%s\n", (wchar_t*)fn2[0]->data);
					}
					console_logw(L"\n%s\n", (wchar_t*)fn->data);
					LOG_WARNING("Navigation error");
				}
#else
				{
					LOG_WARNING("Navigation error");
					array_free(&filenames);
					return 0;
				}
#endif
				Image16 *im2=0;
				int step=key==KEY_RIGHT?1:-1;
				for(int k=currentidx+step;MODVAR(k, k, (int)filenames->count), k!=currentidx;k+=step)
				{
					fn2=(ArrayHandle*)array_at(&filenames, k);
					if(!load_media((wchar_t*)fn2[0]->data, &im2, 0, 0))
					{
						currentidx=k;
						break;
					}
					if(im2)
						image_free(&im2);
				}
				if(im2||animated)
				{
					image_free(&image);
					image_free(&impreview);
					image=im2;
					array_free(&fn);
					fn=filter_pathw((wchar_t*)fn2[0]->data, -1, 0);
					update_image(1, 1);
				}
				array_free(&filenames);
				return 1;
			}
		}
		break;
	case KEY_SPACE:
		videoplayback_pause(0);
		break;
	case 'O':
		if(GET_KEY_STATE(KEY_CTRL))
		{
			ArrayHandle fn2=dialog_open_filew(0, 0, 0);
			if(fn2)
			{
				Image16 *im2=0;
				int error=load_media((wchar_t*)fn2->data, &im2, 1, 0);
				if((im2||animated)&&!error)
				{
					image_free(&image);
					image=im2;
					if(fn)
						array_free(&fn);
					fn=filter_pathw((wchar_t*)fn2->data, -1, 0);
					update_image(1, 0);
				}
				array_free(&fn2);
				return image!=0;
			}
		}
		break;
	case 'F':
		if(animated)
		{
			int shift=GET_KEY_STATE(KEY_SHIFT);
			shift=1-2*shift;//shift = go back
			anim_uitype=(anim_uitype+shift+ANIMUI_COUNT)%ANIMUI_COUNT;
		}
		break;
	case 'M':
		mute_audio=1-mute_audio;
		animation_ctr=ANIMATION_NFRAMES;
		break;
	case 'Q'://equalization
		if(GET_KEY_STATE(KEY_CTRL))
			update_image(0, 0);
		else
			equalize();
		if(hist_on)
			calc_hist();
		return 1;
	case 'G':
		if(imagetype==IM_BAYERv2||imagetype==IM_GRAYSCALEv2)
		{
			if(imagetype==IM_BAYERv2)
				imagetype=IM_GRAYSCALEv2;
			else if(imagetype==IM_GRAYSCALEv2)
				imagetype=IM_BAYERv2;
			update_image(0, 0);
			return 1;
		}
		break;
	case 'E':
		wpx=0, wpy=0, zoom=1;
		imagefitted=0;
		return 1;
	case 'C':
		if(GET_KEY_STATE(KEY_CTRL))//copy screen contents
		{
			if(!image||!impreview)
				break;
			if(zoom<ZOOM_LIMIT_LABEL)
			{
				HPoint2D p1={0, 0}, p2={w, h};

				screen2image(&p1);
				screen2image(&p2);
				//int
				//	x1=screen2image_x_int_rounded(0), y1=screen2image_y_int_rounded(0),
				//	x2=screen2image_x_int_rounded(w), y2=screen2image_y_int_rounded(h);
				if(p1.x<0)p1.x=0;
				if(p1.y<0)p1.y=0;
				if(p2.x>impreview->iw)p2.x=impreview->iw;
				if(p2.y>impreview->ih)p2.y=impreview->ih;
				int x1=(int)floor(p1.x), y1=(int)floor(p1.y);
				int x2=(int)floor(p2.x), y2=(int)floor(p2.y);
				int iw=x2-x1, ih=y2-y1;
				if(iw>0&&ih>0)
				{
					Image8 *crop=image_alloc8(0, iw, ih, 4, 8);
					for(int ky=0, idx=0;ky<ih;++ky)
					{
						for(int kx=0;kx<iw;++kx, idx+=4)
						{
							int idx2=(impreview->iw*(y1+ky)+x1+kx)<<2;
							crop->data[idx+0]=impreview->data[idx2+0];
							crop->data[idx+1]=impreview->data[idx2+1];
							crop->data[idx+2]=impreview->data[idx2+2];
							crop->data[idx+3]=impreview->data[idx2+3];
						}
					}
				//	ImageHandle crop=image_construct(iw, ih, 8, impreview->data+((impreview->iw*(ptrdiff_t)y1+x1)<<2), iw, ih, impreview->xcap-iw, impreview->depth);
					for(ptrdiff_t k=0, res=crop->iw*crop->ih;k<res;++k)//swap red & blue (8-bit)
					{
						unsigned char temp=crop->data[k<<2|0];
						crop->data[k<<2|0]=crop->data[k<<2|2];
						crop->data[k<<2|2]=temp;
					}
					copy_bmp_to_clipboard(crop->data, crop->iw, crop->ih);
					image_free(&crop);
				}
			}
			else
			{
				ArrayHandle str;
				STR_ALLOC(str, 0);
				HPoint2D p1={0, 0}, p2={impreview->iw, impreview->ih};

				image2screen(&p1);
				image2screen(&p2);
				int sx1=(int)ROUND64(p1.x);
				int sy1=(int)ROUND64(p1.y);
				int sx2=(int)ROUND64(p2.x);
				int sy2=(int)ROUND64(p2.y);
				//int
				//	sx1=image2screen_x_int(0), sx2=image2screen_x_int(impreview->iw),
				//	sy1=image2screen_y_int(0), sy2=image2screen_y_int(impreview->ih);
				int csx1=sx1, csx2=sx2;
				int csy1=sy1, csy2=sy2;
				CLAMP2(csx1, 0, w);
				CLAMP2(csx2, 0, w);
				CLAMP2(csy1, 0, h);
				CLAMP2(csy2, 0, h);
				p1.x=csx1; p1.y=csy1;
				p2.x=csx2; p2.y=csy2;
				screen2image(&p1);
				screen2image(&p2);
				int ix1=(int)ROUND64(p1.x);
				int iy1=(int)ROUND64(p1.y);
				int ix2=(int)ROUND64(p2.x);
				int iy2=(int)ROUND64(p2.y);
				//int ix1=screen2image_x_int(csx1), ix2=screen2image_x_int(csx2);
				//int iy1=screen2image_y_int(csy1), iy2=screen2image_y_int(csy2);
				const char *format;
				if(pxlabels_hex)
				{
					if(imagedepth<=4)
						format=" 0x%01X";
					else if(imagedepth<=8)
						format=" 0x%02X";
					else if(imagedepth<=12)
						format=" 0x%03X";
					else
						format=" 0x%04X";
				}
				else
					format=" %5d";
				switch(imagetype)
				{
				default://make gcc happy
					break;
				case IM_GRAYSCALEv2:
					{
						str_append(&str, "%d bit GRAY:\n", imagedepth);
						const unsigned short *ptr=(const unsigned short*)image->data;
						int iy=MAXVAR(iy1, 0), yend=MINVAR(iy2+2, image->ih);
						for(;iy<yend;++iy)
						{
							//int ky=image2screen_y_int(iy);
							int ix=MAXVAR(ix1, 0), xend=MINVAR(ix2+2, image->iw);
							for(;ix<xend;++ix)
							{
								//int kx=image2screen_x_int(ix);
								str_append(&str, format, ptr[image->iw*iy+ix]);
							}
							str_append(&str, "\n");
						}
					}
					break;
				case IM_RGBA:
					{
						str_append(&str, "%d bit RGBA:\n", imagedepth);
						const unsigned short *ptr=(const unsigned short*)image->data;
						int iy=MAXVAR(iy1, 0), yend=MINVAR(iy2+2, image->ih);
						for(;iy<yend;++iy)
						{
							//int ky=image2screen_y_int(iy);
							int ix=MAXVAR(ix1, 0), xend=MINVAR(ix2+2, image->iw);
							for(;ix<xend;++ix)
							{
								//int kx=image2screen_x_int(ix);
								int idx=(image->iw*iy+ix)<<2;
								str_append(&str, "    ");
								str_append(&str, format, ptr[idx+0]);
								str_append(&str, format, ptr[idx+1]);
								str_append(&str, format, ptr[idx+2]);
								str_append(&str, format, ptr[idx+3]);
							}
							str_append(&str, "\n");
						}
					}
					break;
				case IM_BAYERv2:
					{
						const char labels[]="RGB";
						const unsigned short *ptr=(const unsigned short*)image->data;
						str_append(&str, "%d bit %c%c%c%c:\n"
							, imagedepth
							, labels[(int)bayer[0]]
							, labels[(int)bayer[1]]
							, labels[(int)bayer[2]]
							, labels[(int)bayer[3]]
						);
						int iy=MAXVAR(iy1, 0)&~1, yend=MINVAR(iy2+2, image->ih);
						for(;iy<yend;++iy)
						{
							//int ky=image2screen_y_int(iy);
							int ix=MAXVAR(ix1, 0)&~1, xend=MINVAR(ix2+2, image->iw);
							for(;ix<xend;++ix)
							{
								//int kx=image2screen_x_int(ix);
								str_append(&str, format, ptr[image->iw*iy+ix]);
							//	int idx=(image->iw*iy+ix)<<2;
							//	str_append(&str, format, ptr[idx+bayer[(iy&1)<<1|ix&1]]>>(16-imagedepth));
							}
							str_append(&str, "\n");
						}
					}
#if 0
					{
						const char labels[]="RGB";
						str_append(&str, "%d bit %c%c%c%c:\n", imagedepth, labels[bayer[0]], labels[bayer[1]], labels[bayer[2]], labels[bayer[3]]);
						unsigned short *ptr=(unsigned short*)image->data;
						int iy=MAXVAR(iy1, 0);
						iy>>=1;
						iy<<=1;
						for(int yend=MINVAR(iy2+2, image->ih);iy<yend;++iy)
						{
							int ky=image2screen_y_int(iy);
							int ix=MAXVAR(ix1, 0);
							ix>>=1;
							ix<<=1;
							for(int xend=MINVAR(ix2+2, image->iw);ix<xend;++ix)
							{
								int kx=image2screen_x_int(ix);
								int idx=(image->iw*iy+ix)<<2;
								str_append(&str, format, ptr[idx+bayer[(iy&1)<<1|ix&1]]>>(16-imagedepth));
							}
							str_append(&str, "\n");
						}
					}
#endif
					break;
				}
				copy_to_clipboard((char*)str->data, (int)str->count);
				array_free(&str);
			}
		}
		else if(impreview)
			image_fit2screen(impreview->iw, impreview->ih);
		return 1;
	case 'V':
		if(GET_KEY_STATE(KEY_CTRL))
		{
			Image8 *im2=paste_bmp_from_clipboard();
			if(im2)
			{
				image_free(&image);
				image_free(&impreview);
				image=image_alloc16(0, im2->iw, im2->ih, 4, 4, 256, 8);
				for(ptrdiff_t k=0, res=(ptrdiff_t)4*im2->iw*im2->ih;k<res;++k)
					image->data[k]=im2->data[k];
			//	image=image_construct(im2->iw, im2->ih, 16, im2->data, im2->iw, im2->ih, im2->xcap-im2->iw, im2->depth);
				impreview=im2;
				imagetype=IM_RGBA;
				imagedepth=im2->srcdepth;
				update_image(0, 0);
				if(imagefitted)
					image_fit2screen(image->iw, image->ih);
				return 1;
			}
		}
		break;
	case 'P':
		if(animated)
		{
			int step=GET_KEY_STATE(KEY_SHIFT)?-1:1;
			playopt=(playopt+step+PLAYOPT_COUNT)%PLAYOPT_COUNT;
			animation_ctr=ANIMATION_NFRAMES;
		}
		else
			pixelpreview=!pixelpreview;
		return 1;
	case 'H':
		if(GET_KEY_STATE(KEY_CTRL))//toggle pixel label base
			pxlabels_hex=!pxlabels_hex;
		else//toggle histogram
		{
			hist_on=!hist_on;
			if(hist_on)
				calc_hist();
		}
		return 1;
	case 'X':
		if(GET_KEY_STATE(KEY_CTRL))//horizontal flip
		{
			image_inplacexflip(image, bayer);
			update_image(0, 1);
			break;
		}
		else//toggle horizontal profile plot
		{
			if(profileplotmode!=PROFILE_X)
				profileplotmode=PROFILE_X;
			else
				profileplotmode=PROFILE_OFF;
		}
		return 1;
	case 'Y':
		if(GET_KEY_STATE(KEY_CTRL))//vertical flip
		{
			image_inplaceyflip(image, bayer);
			update_image(0, 1);
			break;
		}
		else//toggle vertical profile plot
		{
			if(profileplotmode!=PROFILE_Y)
				profileplotmode=PROFILE_Y;
			else
				profileplotmode=PROFILE_OFF;
		}
		return 1;
	case 'R':case KEY_F5:
		if(key=='R'&&GET_KEY_STATE(KEY_CTRL))//rotate 90 degrees
		{
			if(GET_KEY_STATE(KEY_SHIFT))//clockwise
			{
				image_inplacexflip(image, bayer);
				image_transpose(&image, bayer);
			}
			else//counter-clockwise
			{
				image_transpose(&image, bayer);
				image_inplacexflip(image, bayer);
			}
			update_image(0, 1);
			break;
		}
		else if(fn)
		{
			Image16 *im2=0;
			load_media((wchar_t*)fn->data, &im2, 0, 0);
			if(im2)
			{
				image_free(&image);
				image=im2;
				update_image(0, 1);
			}
			g_timescale=1;
		}
		break;
	case 'T':
		if(GET_KEY_STATE(KEY_CTRL))//transpose
		{
			image_transpose(&image, bayer);
			update_image(0, 1);
		}
		break;
	case KEY_QUOTE://toggle bitplane view
		switch(imagetype)
		{
		default://make gcc happy
			break;
		case IM_GRAYSCALEv2:
			bitmode=!bitmode;
			break;
		case IM_RGBA:
			bitmode+=1-(GET_KEY_STATE(KEY_SHIFT)<<2);
			MODVAR(bitmode, bitmode, 3);
			break;
		case IM_BAYERv2:
			bitmode=!bitmode;
			break;
		}
		bitplane=bitplane_mreduce(bitplane);
		update_image(0, 1);
		break;
	case KEY_LBRACKET://'[': prev bitplane  MSB <- LSB
	case KEY_RBRACKET://']': next bitplane  MSB -> LSB
		if(bitmode)
		{
			bitplane+=2*(key==KEY_LBRACKET)-1;
			bitplane=bitplane_mreduce(bitplane);
			update_image(0, 1);
		}
		break;
	}
	return 0;
}
int io_keyup(IOKey key, char c)
{
	switch(key)
	{
	default://make gcc happy
		break;
	case KEY_LBUTTON:
		if(drag==DRAG_SEEK)
		{
			drag=DRAG_NONE;
		//	slider_set((double)mx/w, 0);
			mouse_release();
		}
		else if(drag==DRAG_IMAGE)//stop dragging
		{
			drag=DRAG_NONE;
			mouse_release();
			set_mouse(start_mx, start_my);
			show_mouse(1);
		}
		break;

#define		AK(KEY)		case KEY:
	ACTIVE_KEY_LIST
#undef		AK
		count_active_keys(key);
		break;
	}
	return 0;
}
void io_timer()
{
	const int delta=10;//screen pixels per frame
	if(keyboard[KEY_ENTER])
		zoom_at(w>>1, h>>1, 1.02);
	if(keyboard[KEY_BKSP])
		zoom_at(w>>1, h>>1, 1/1.02);
	if(keyboard['W'])//move window up
		wpy-=delta/zoom;
	if(keyboard['A'])//move window left
		wpx-=delta/zoom;
	if(keyboard['S'])//move window down
		wpy+=delta/zoom;
	if(keyboard['D'])//move window right
		wpx+=delta/zoom;

	invalidate();
}
static void print_pixellabels(int ix1, int ix2, int iy1, int iy2, int xoffset, int yoffset, int component, char label, uint64_t txtcolors, int is_bayer, int tight)
{
	unsigned short *ptr=image->data+(((ptrdiff_t)image->iw*yoffset+xoffset)<<(imagetype==IM_RGBA?2:0));
	const char *format;
	if(pxlabels_hex)
	{
		if(imagedepth<=4)
			format="%c %01X"+tight*3;
		else if(imagedepth<=8)
			format="%c %02X"+tight*3;
		else if(imagedepth<=12)
			format="%c %03X"+tight*3;
		else
			format="%c%04X"+tight*2;
	}
	else
		format="%c%5d"+tight*2;
	txtcolors=set_text_colors(txtcolors);
	int iy=MAXVAR(iy1, 0), yend=MINVAR(iy2+2, image->ih);
	iy>>=is_bayer;
	iy<<=is_bayer;
	float fontsize=1, labelxoffset=0, labelyoffset=0;
	if(is_bayer)
	{
		labelxoffset=-(float)(zoom*0.5*xoffset);
		labelyoffset=-(float)(zoom*0.5*yoffset);
	}
	else
	{
		labelxoffset=0;
		labelyoffset=tdy*fontsize*component;
	}
	for(;iy<yend;++iy)
	{
		float y=(float)(iy+yoffset);
	//	int ky=image2screen_y_int(y);
		int ix=MAXVAR(ix1, 0), xend=MINVAR(ix2+2, image->iw);
		for(;ix<xend;++ix)
		{
			float x=(float)(ix+xoffset);
			HPoint2D p={x, y};
			image2screen(&p);
			int kx=(int)floor(p.x);
			int ky=(int)floor(p.y);
		//	int kx=image2screen_x_int(x);
			int idx=image->iw*(iy<<1)+(ix<<1);
			if(imagetype==IM_RGBA)
				idx<<=2;
			if(tight)
				GUIPrint_enqueue(&vertices_text, 0, (float)kx+labelxoffset, (float)ky+labelyoffset, fontsize, format, ptr[idx+component]);
			else
				GUIPrint_enqueue(&vertices_text, 0, (float)kx+labelxoffset, (float)ky+labelyoffset, fontsize, format, label, ptr[idx+component]);
		}
	}
	print_line_flush(vertices_text, fontsize);
	txtcolors=set_text_colors(txtcolors);
}
static void print_pixellabels_preview(int ix1, int ix2, int iy1, int iy2, int xoffset, int yoffset, int component, char label, uint64_t txtcolors, int is_bayer, int tight)
{
	unsigned char *ptr=impreview->data+(((ptrdiff_t)impreview->iw*yoffset+xoffset)<<2);
	const char *format;
	if(pxlabels_hex)
		format="%c %02X"+tight*3;
	else
		format="%c%5d"+tight*2;
	txtcolors=set_text_colors(txtcolors);
	int iy=MAXVAR(iy1, 0), yend=MINVAR(iy2+2, impreview->ih);
	iy>>=is_bayer;
	iy<<=is_bayer;
	float fontsize=1, labeloffset=is_bayer?0:tdy*fontsize*component;
	for(;iy<yend;iy+=1<<is_bayer)
	{
	//	int ky=image2screen_y_int(iy+yoffset);
		int ix=MAXVAR(ix1, 0), xend=MINVAR(ix2+2, impreview->iw);
		ix>>=is_bayer;
		ix<<=is_bayer;
		for(;ix<xend;ix+=1<<is_bayer)
		{
			HPoint2D p={ix+xoffset, iy+yoffset};
			image2screen(&p);
			int kx=(int)floor(p.x);
			int ky=(int)floor(p.y);
		//	int kx=image2screen_x_int(ix+xoffset);
			int idx=impreview->iw*iy+ix;
			//if(imagetype==IM_RGBA)
				idx<<=2;
			if(tight)
				GUIPrint_enqueue(&vertices_text, 0, (float)kx, (float)ky+labeloffset, fontsize, format, ptr[idx+component]);
			else
				GUIPrint_enqueue(&vertices_text, 0, (float)kx, (float)ky+labeloffset, fontsize, format, label, ptr[idx+component]);
		}
	}
	print_line_flush(vertices_text, fontsize);
	txtcolors=set_text_colors(txtcolors);
}
static void draw_histogram(int *hist, int nlevels, int color, int y1, int y2)
{
	int fmax=0;
	for(int sym=0;sym<nlevels;++sym)
	{
		if(fmax<hist[sym])
			fmax=hist[sym];
	}
	for(int sym=0;sym<nlevels;++sym)
		draw_rect_enqueue(&vertices_2d, (float)sym*w/nlevels, (float)(sym+1)*w/nlevels, y2-(float)hist[sym]*(y2-y1)/fmax, (float)y2);
	draw_2d_flush(vertices_2d, color, GL_TRIANGLES);
}
static void draw_profile(int comp, int color, int profx, int frompreview)//horizontal cross-section profile		to see the color/spatial correlation
{
	int nlevels=(1<<imagedepth)-1;
	if(profx)
	{
		float scale=(float)h*0.5f/(float)nlevels;
		for(int kx=0;kx<w;++kx)
		{
			HPoint2D p={kx, my};
		
			screen2image(&p);
			int ix=(int)floor(p.x);
			int iy=(int)floor(p.y);
			int pixel=0;
			if((uint32_t)ix<(uint32_t)impreview->iw&&(uint32_t)iy<(uint32_t)impreview->ih)
			{
				if(frompreview)
					pixel=impreview->data[4*(impreview->iw*iy+ix)+comp];
				else
				{
					switch(imagetype)
					{
					case IM_GRAYSCALEv2:
						pixel=image->data[image->iw*iy+ix];
						break;
					case IM_RGBA:
						pixel=image->data[4*(image->iw*iy+ix)+comp];
						break;
					case IM_BAYERv2:
						pixel=image->data[image->iw*((iy&~1)|comp>>1)+2*ix+(comp&1)];
						break;
					default://stupid GCC warnings
						break;
					}
				}
			}
			draw_curve_enqueue(&vertices_2d, (float)kx, (float)pixel*scale);
		}
	}
	else
	{
		float scale=(float)w*0.5f/(float)nlevels;
		for(int ky=0;ky<h;++ky)
		{
			HPoint2D p={mx, ky};
		
			screen2image(&p);
			int ix=(int)floor(p.x);
			int iy=(int)floor(p.y);
			int pixel=0;
			if((uint32_t)ix<(uint32_t)impreview->iw&&(uint32_t)iy<(uint32_t)g_ih)
			{
				if(frompreview)
					pixel=impreview->data[4*(impreview->iw*iy+ix)+comp];
				else
				{
					switch(imagetype)
					{
					case IM_GRAYSCALEv2:
						pixel=image->data[image->iw*iy+ix];
						break;
					case IM_RGBA:
						pixel=image->data[4*(image->iw*iy+ix)+comp];
						break;
					case IM_BAYERv2:
						pixel=image->data[image->iw*((iy&~1)|comp>>1)+2*ix+(comp&1)];
						break;
					default://stupid GCC warnings
						break;
					}
				}
			}
			draw_curve_enqueue(&vertices_2d, (float)pixel*scale, (float)ky);
		}
	}
	draw_2d_flush(vertices_2d, color, GL_LINE_STRIP);
#if 0
	int iy=screen2image_y_int(my);
	if((unsigned)iy<(unsigned)image->ih)
	{
		unsigned short *row=0;
		int lgstride=0;
		switch(imagetype)
		{
		default://make gcc happy
			break;
		case IM_GRAYSCALEv2:
			row=image->data+((size_t)image->iw*iy);
			lgstride=0;
			break;
		case IM_RGBA:
			row=image->data+((size_t)image->iw*iy<<2|comp);
			lgstride=2;
			break;
		case IM_BAYERv2://FIXME Bayer matrix
			iy=(iy&~1)|comp>>1;
			row=image->data+image->iw*iy+(comp&1);
		//	row=image->data+((image->iw*iy+(comp&1))<<2|bayer[comp]);
			lgstride=2;
			break;
		}
		int ix;
		float y2;
		int nlevels=(1<<imagedepth)-1;
		float gain=(h>>1)/(float)nlevels;
		int mask=imagetype==IM_BAYERv2;
		for(int kx=0;kx<w;++kx)
		{
			ix=screen2image_x_int(kx);
			ix&=~mask;
			if((unsigned)ix<(unsigned)image->iw)
				y2=h-tdy-row[ix<<lgstride]*gain;
			else
				y2=h-tdy;
			draw_curve_enqueue(&vertices_2d, (float)kx, y2);
		}
		draw_2d_flush(vertices_2d, color, GL_LINE_STRIP);
	}
#endif
}
#if 0
static void draw_profile_y(int comp, int color, int frompreview)//vertical cross-section profile
{
	int nlevels=(1<<imagedepth)-1;
	float scale=(float)w*0.5f/(float)nlevels;
	for(int ky=0;ky<h;++ky)
	{
		HPoint2D p={mx, ky};
		
		screen2image(&p);
		int ix=(int)floor(p.x);
		int iy=(int)floor(p.y);
		int pixel=0;
		if((uint32_t)ix<(uint32_t)g_iw&&(uint32_t)iy<(uint32_t)g_ih)
		{
			if(frompreview)
				pixel=impreview->data[4*(g_iw*iy+ix)+comp];
			else
			{
				switch(imagetype)
				{
				case IM_GRAYSCALEv2:
					pixel=image->data[image->iw*iy+ix];
					break;
				case IM_RGBA:
					pixel=image->data[4*(image->iw*iy+ix)+comp];
					break;
				case IM_BAYERv2:
					pixel=image->data[image->iw*((iy&~1)|comp>>1)+2*ix+(comp&1)];
					break;
				}
			}
		}
		draw_curve_enqueue(&vertices_2d, (float)pixel*scale, (float)ky);
	}
	draw_2d_flush(vertices_2d, color, GL_LINE_STRIP);
#if 0
	int ix=screen2image_x_int(mx);
	if((unsigned)ix<(unsigned)image->iw)
	{
		unsigned short *col=0;
		int stride=0;
		switch(imagetype)
		{
		default://make gcc happy
			break;
		case IM_GRAYSCALEv2:
			col=image->data+ix;
			stride=image->iw;
			break;
		case IM_RGBA:
			col=image->data+((size_t)ix<<2|comp);
			stride=image->iw<<2;
			break;
		case IM_BAYERv2://FIXME Bayer matrix
			ix=(ix&~1)|(comp&1);
			col=image->data+(image->iw&-(comp>>1))+ix;
		//	col=image->data+((ix+(image->iw&-(comp>>1)))<<2|bayer[comp]);
			stride=image->iw<<1;
			break;
		}
		
		float x2;
		int iy;
		int nlevels=(1<<imagedepth)-1;
		float gain=(w>>1)/(float)nlevels;
		int mask=imagetype==IM_BAYERv2;
		for(int ky=0;ky<h;++ky)
		{
			iy=screen2image_y_int(ky);
			iy&=~mask;
			if((unsigned)iy<(unsigned)image->ih)
				x2=col[iy*stride]*gain;
			else
				x2=0;
			draw_curve_enqueue(&vertices_2d, x2, (float)ky);
		}
		draw_2d_flush(vertices_2d, color, GL_LINE_STRIP);
	}
#endif
}
static void draw_profile_x_preview(int comp, int color)//horizontal cross-section profile		to see the color/spatial correlation
{
	int iy=screen2image_y_int(my);
	if((unsigned)iy<(unsigned)impreview->ih)
	{
		unsigned char *row=impreview->data+((size_t)impreview->iw*iy<<2|comp);
		int lgstride=2;
		int ix;
		float y2;
		float gain=(h>>1)/255.f;
		int mask=imagetype==IM_BAYERv2;
		for(int kx=0;kx<w;++kx)
		{
			ix=screen2image_x_int(kx);
			ix&=~mask;
			if((unsigned)ix<(unsigned)impreview->iw)
				y2=h-tdy-row[ix<<lgstride]*gain;
			else
				y2=h-tdy;
			draw_curve_enqueue(&vertices_2d, (float)kx, y2);
		}
		draw_2d_flush(vertices_2d, color, GL_LINE_STRIP);
	}
}
static void draw_profile_y_preview(int comp, int color)//vertical cross-section profile
{
	int ix=screen2image_x_int(mx);
	if((unsigned)ix<(unsigned)impreview->iw)
	{
		unsigned char *col=impreview->data+((size_t)ix<<2|comp);
		int stride=impreview->iw<<2;
		
		float x2;
		int iy;
		float gain=(w>>1)/255.f;
		int mask=imagetype==IM_BAYERv2;
		for(int ky=0;ky<h;++ky)
		{
			iy=screen2image_y_int(ky);
			iy&=~mask;
			if((unsigned)iy<(unsigned)impreview->ih)
				x2=col[iy*stride]*gain;
			else
				x2=0;
			draw_curve_enqueue(&vertices_2d, x2, (float)ky);
		}
		draw_2d_flush(vertices_2d, color, GL_LINE_STRIP);
	}
}
#endif
static void print_time(float x, float y, float zoom, double t)
{
	if(t<60)
		GUIPrint(0, x, y, zoom, "%g", t);
	//if(t<10)
	//	GUIPrint(0, x, y, zoom, "%4.2lf", t);
	//else if(t<60)
	//	GUIPrint(0, x, y, zoom, "%4.1lf", t);
	else if(t<60*60)
	{
		int minutes=(int)(t/60);
		double seconds=t-minutes*60;
		GUIPrint(0, x, y, zoom, "%d:%02.0lf", minutes, seconds);
	}
	else
	{
		double seconds=t;
		int hours=(int)(seconds/(60*60));
		seconds-=hours*60*60;
		int minutes=(int)(seconds/60);
		seconds-=minutes*60;
		GUIPrint(0, x, y, zoom, "%d:%02d:%02.0lf", hours, minutes, seconds);
	}
}
static void print_duration(float x, float y, float zoom, double t)
{
	double seconds=t;
	int hours=(int)(seconds/(60*60));
	seconds-=hours*60*60;
	int minutes=(int)(seconds/60);
	seconds-=minutes*60;
	GUIPrint(0, x, y, zoom, "%d:%02d:%06.3lf", hours, minutes, seconds);
}
void io_render()
{
	Slider slider={0};
//#ifdef PROFILE_FPS
//	static double trender=0;
//	double trender_current=0;
//#endif
		//static float rads=0;
		//rads+=0.001f;
		//g_rotationmatrix[0]=cosf(rads);
		//g_rotationmatrix[1]=-sinf(rads);
		//g_rotationmatrix[2]=-g_rotationmatrix[1];
		//g_rotationmatrix[3]=g_rotationmatrix[0];
#ifdef PROFILE_FPS
	recordevent(EVNT_RENDER, 1);
	//fps_checkpoints[fps_start].t=time_sec();
	//fps_start=(fps_start+1)
	//trender_current=time_sec();
#endif
	if(h<=0)
		return;
	if(animated&&!playing)
		playbackendaction();
	if(g_averror)
	{
		static uint32_t LOL_1=0;
		++LOL_1;
		if(LOL_1<=1)
			messageboxa(MBOX_OK, "Error", "(%d) %d: %s", g_avline, g_averror, ffmpegerror);
	}
	if(animated)
	{
		slider_get(&slider);
		if(slider.playing)
		{
//#ifdef PROFILE_FPS
//			tsend2gpu=time_sec();
//#endif
			if(!frame_dequeue())
			{
				if(impreview&&hist_on)
					calc_hist();
			}
			//else if(has_video)
			//	return;
//#ifdef PROFILE_FPS
//			tsend2gpu=time_sec()-tsend2gpu;
//#endif
		}
	}
	if(!background[3])
	{
		background[3]=255;
		glClearColor(background[0]/255.f, background[1]/255.f, background[2]/255.f, 1);
	}
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

	if(impreview||animated)
	{
		int imx=0, imy=0;

		if(impreview)
		{
			HPoint2D sp1={0, 0};
			HPoint2D sp2={0, impreview->ih};
			HPoint2D sp3={impreview->iw, impreview->ih};
			HPoint2D sp4={impreview->iw, 0};
			HPoint2D ipm={mx, my};

			image2screen(&sp1);
			image2screen(&sp2);
			image2screen(&sp3);
			image2screen(&sp4);
			screen2image(&ipm);
			int sx1=(int)floor(sp1.x);
			int sy1=(int)floor(sp1.y);
			int sx2=(int)floor(sp2.x);
			int sy2=(int)floor(sp2.y);
			imx=(int)floor(ipm.x);
			imy=(int)floor(ipm.y);
			//int
			//	sx1=image2screen_x_int(0), sx2=image2screen_x_int(impreview->iw),
			//	sy1=image2screen_y_int(0), sy2=image2screen_y_int(impreview->ih);
			//
			//imx=screen2image_x_int(mx);
			//imy=screen2image_y_int(my);
			{
				static float ndc[16];
				int j;
				
				j=0;
				ndc[j++]=(float)sp1.x; ndc[j++]=(float)sp1.y; ndc[j++]=0; ndc[j++]=0;//top left
				ndc[j++]=(float)sp2.x; ndc[j++]=(float)sp2.y; ndc[j++]=0; ndc[j++]=1;//bottom left
				ndc[j++]=(float)sp3.x; ndc[j++]=(float)sp3.y; ndc[j++]=1; ndc[j++]=1;//bottom right
				ndc[j++]=(float)sp4.x; ndc[j++]=(float)sp4.y; ndc[j++]=1; ndc[j++]=0;//top right
				//{
				//	HPoint2D c={g_iw*0.5, g_ih*0.5};
				//
				//	image2screen(&c);
				//	for(int k=0;k<4;++k)
				//	{
				//		float x=ndc[4*k+0]-(float)c.x, y=ndc[4*k+1]-(float)c.y;
				//		ndc[4*k+0]=(float)(g_rotationmatrix[0]*x+g_rotationmatrix[1]*y+c.x);
				//		ndc[4*k+1]=(float)(g_rotationmatrix[2]*x+g_rotationmatrix[3]*y+c.y);
				//	}
				//}
#if 0
				if(g_rotation!=ROTATE_NORMAL)
				{
					float cx=(float)image2screen_x(impreview->iw*0.5);
					float cy=(float)image2screen_y(impreview->ih*0.5);
					float matrix[4]={1, 0, 0, 1};

					for(int k=0;k<4;++k)
					{
						float x=ndc[4*k+0]-cx, y=ndc[4*k+1]-cy;
						ndc[4*k+0]=matrix[0]*x+matrix[1]*y+cx;
						ndc[4*k+1]=matrix[2]*x+matrix[3]*y+cy;
					}
				}
#endif
				{
					float xscale=2.f/(float)w;
					float yscale=2.f/(float)h;
					for(int k=0;k<4;++k)
					{
						ndc[4*k+0]=ndc[4*k+0]*xscale-1;
						ndc[4*k+1]=1-ndc[4*k+1]*yscale;
					}
				}
				display_texture_ndc(ndc, image_txid, 1);
			}
			//display_texture(sx1, sx2, sy1, sy2
			//	, image_txid, 1
			//	, 0, 1, 0, 1
			//	, ROTATE_ARBITRARY
			//	, (float)image2screen_x(impreview->iw*0.5)
			//	, (float)image2screen_y(impreview->ih*0.5)
			//	, (float)time_sec()
			//);
		//	display_texture(sx1, sx2, sy1, sy2, image_txid, 1, 0, 1, 0, 1, g_rotation);
		//	display_texture_i(sx1, sx2, sy1, sy2, (int*)impreview->data, impreview->iw, impreview->ih, 0, 1, 0, 1, 1, 0, 1);
			if(zoom>=ZOOM_LIMIT_LABEL)
			{
				int tight=zoom<ZOOM_LIMIT_LABEL*2;
				int csx1=sx1, csx2=sx2;
				int csy1=sy1, csy2=sy2;
				CLAMP2(csx1, 0, w);
				CLAMP2(csx2, 0, w);
				CLAMP2(csy1, 0, h);
				CLAMP2(csy2, 0, h);
				HPoint2D ip1={csx1, csy1}, ip2={csx2, csy2};

				screen2image(&ip1);
				screen2image(&ip2);
				int ix1=(int)floor(ip1.x);
				int iy1=(int)floor(ip1.y);
				int ix2=(int)floor(ip2.x);
				int iy2=(int)floor(ip2.y);
			//	int ix1=screen2image_x_int(csx1), ix2=screen2image_x_int(csx2);
			//	int iy1=screen2image_y_int(csy1), iy2=screen2image_y_int(csy2);
				const char labels[]="rgb";
				uint64_t theme[]=
				{
					0xC00000FF80000000,
					0xC000FF0080000000,
					0xC0FF000080FFFFFF,
					0xC0FFFFFF80000000,
				};
				if(pixelpreview)
				{
					print_pixellabels_preview(ix1, ix2, iy1, iy2, 0, 0, 0, 'r', theme[0], 0, tight);
					print_pixellabels_preview(ix1, ix2, iy1, iy2, 0, 0, 1, 'g', theme[1], 0, tight);
					print_pixellabels_preview(ix1, ix2, iy1, iy2, 0, 0, 2, 'b', theme[2], 0, tight);
					if(zoom>=ZOOM_LIMIT_ALPHA)
						print_pixellabels_preview(ix1, ix2, iy1, iy2, 0, 0, 3, 'a', theme[3], 0, tight);
				}
				else if(imagetype==IM_GRAYSCALEv2)
					print_pixellabels(ix1, ix2, iy1, iy2, 0, 0, 0, 'g', theme[3], 0, tight);
				else if(imagetype==IM_RGBA)
				{
					print_pixellabels(ix1, ix2, iy1, iy2, 0, 0, 0, 'r', theme[0], 0, tight);
					print_pixellabels(ix1, ix2, iy1, iy2, 0, 0, 1, 'g', theme[1], 0, tight);
					print_pixellabels(ix1, ix2, iy1, iy2, 0, 0, 2, 'b', theme[2], 0, tight);
					if(zoom>=ZOOM_LIMIT_ALPHA)
						print_pixellabels(ix1, ix2, iy1, iy2, 0, 0, 3, 'a', theme[3], 0, tight);
				}
				else if(imagetype==IM_BAYERv2)
				{
					print_pixellabels(ix1, ix2, iy1, iy2, 0, 0, 0, labels[(int)bayer[0]], theme[(int)bayer[0]], 1, tight);
					print_pixellabels(ix1, ix2, iy1, iy2, 1, 0, 0, labels[(int)bayer[1]], theme[(int)bayer[1]], 1, tight);
					print_pixellabels(ix1, ix2, iy1, iy2, 0, 1, 0, labels[(int)bayer[2]], theme[(int)bayer[2]], 1, tight);
					print_pixellabels(ix1, ix2, iy1, iy2, 1, 1, 0, labels[(int)bayer[3]], theme[(int)bayer[3]], 1, tight);
				}
			}
			if(hist_on)
			{
				switch(imagetype)
				{
				default://make gcc happy
					break;
				case IM_GRAYSCALEv2:
					draw_histogram(histogram, 256, 0x80808080, (int)tdy, (int)(h-tdy));
					break;
				case IM_RGBA:
				case IM_BAYERv2:
					{
						int y1=(int)tdy, y2=(int)(h-tdy), dy=(int)(y2-y1);
						draw_histogram(histogram    , 256, 0x800000FF, y1       , y1+dy/3	);
						draw_histogram(histogram+256, 256, 0x8000FF00, y1+dy  /3, y1+dy*2/3	);
						draw_histogram(histogram+512, 256, 0x80FF0000, y1+dy*2/3, y2		);
					}
					break;
				}
			}
		}
		if(animated&&anim_uitype!=ANIMUI_CLEAN)
		{
			enum
			{
				UNIT_SECONDS,
				UNIT_MINUTES,
				UNIT_HOURS,

				SCALE=128,
			};
			draw_rect(0, (float)(w*slider.timestamp/slider.duration), (float)(h-tdy-SLIDER_HEIGHT-1), (float)(h-tdy), 0x804080C0);
			double step=SCALE*slider.duration/w;
			int unit=UNIT_SECONDS;
			if(step>60)
				unit=UNIT_MINUTES;
			if(step>60*60)
				unit=UNIT_HOURS;
			switch(unit)
			{
			case UNIT_SECONDS:break;
			case UNIT_MINUTES:step*=1./60;break;
			case UNIT_HOURS:step*=1./(60*60);break;
			}
			int p=floor_log10(step);
			int msd=(int)(step*_10pow(-p));
			static const int digits[]=
			{//	0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
				1, 1, 2, 2, 2, 5, 5, 5, 5, 5,
			};
			step=digits[msd]*_10pow(p);
			switch(unit)
			{
			case UNIT_SECONDS:break;
			case UNIT_MINUTES:step*=60;break;
			case UNIT_HOURS:step*=60*60;break;
			}
			
			enum
			{
				PLAYER_COLOR=0xFFFFC080,
			};
			int bk0=set_bk_color(PLAYER_COLOR);
			for(double tick=step;tick<slider.duration;tick+=step)
			{
				float x=(float)(w*tick/slider.duration);
				draw_line(x, (float)(h-SLIDER_HEIGHT), x, (float)(h-tdy), PLAYER_COLOR);
			//	draw_line(x, (float)(h-SLIDER_HEIGHT), x, (float)(h-tdy), 0xFFABABAB);
			//	draw_line(x, (float)(h-SLIDER_HEIGHT), x, (float)(h-tdy), 0x80FF0000);
			}
			for(double tick=step;tick<slider.duration;tick+=step)
				print_time((float)(w*tick/slider.duration), (float)(h-tdy-SLIDER_HEIGHT), 1, tick);
			print_duration(0, (float)(h-3*tdy-SLIDER_HEIGHT), 2, slider.timestamp);
			print_duration((float)(w-200), (float)(h-3*tdy-SLIDER_HEIGHT), 2, slider.duration);
			if(animation_ctr>0)
			{
				draw_rect((float)(w-SLIDER_HEIGHT), (float)w, (float)(h-h/2.f*g_volume), (float)h, 0x80C08040);
				if(mute_audio)
					GUIPrint(0, (float)(w-200), h/2.f, 2, "Muted");
				else
					GUIPrint(0, (float)(w-200), h/2.f, 2, "%8.4lf%%", 100.*g_volume);
				GUIPrint(0, 0, h/2.f-3*2*tdy, 2, "%c LOOP", playopt==PLAYOPT_LOOP?'>':' ');
				GUIPrint(0, 0, h/2.f-2*2*tdy, 2, "%c ONCE", playopt==PLAYOPT_ONCE?'>':' ');
				GUIPrint(0, 0, h/2.f-1*2*tdy, 2, "%c LIST", playopt==PLAYOPT_LIST?'>':' ');
				GUIPrint(0, 0, h/2.f+0*2*tdy, 2, "%c SHUF", playopt==PLAYOPT_SHUF?'>':' ');
				GUIPrint(0, w/2.f, (float)(h-3*tdy-SLIDER_HEIGHT), 2, "%12.3lfx", g_timescale);
				--animation_ctr;
			}
#ifdef PRINT_DEBUGTS
			GUIPrint(0, w/2.f, h/2.f, 2, "%12.6lf", g_debugts);
#endif
#ifdef ENABLE_SUBTITLES
			if(slider.timestamp>=sub_tstart&&slider.timestamp<=sub_tend)
			{
				set_bk_color(0x40404040);
				for(int k=0;k<sub_nlines;++k)
					GUIPrint(0, 0, h-tdy-SLIDER_HEIGHT-tdy*(sub_nlines-k), 1, "%s", sub_buf+sub_lines[k]);
			}
#endif
			set_bk_color(bk0);
#if defined PROFILE_FPS
			if(anim_uitype==ANIMUI_STATS)
			{
				double graphwidth=100;
				float xscale, yscale=(float)h*(0.125f/EVNT_COUNT);
				double now, t, toldest;
				int k;
				
				toldest=HUGE_VALF;
				for(k=0;k<FPS_QUEUEMASK;++k)
				{
					ProfileEvent *e;

					e=fps_checkpoints+k;
					if(toldest>e->t)
						toldest=e->t;
				}
				now=time_sec();
				t=now-toldest;
				if(t>graphwidth)
					t=graphwidth;
				xscale=(float)(w/t);
				t=now;
			//	for(double t2=floor(10*now)*0.1;t2>now-5;t2-=0.1)
				//for(double t2=now;t2>now-graphwidth;t2-=slider.framedelta)
				//{
				//	draw_line((float)(now-t2)*xscale
				//		, (float)h*0.125f
				//		, (float)(now-t2)*xscale
				//		, (float)h*0.25f
				//		, 0xFF000000
				//	);
				//	GUIPrint(0, (float)(now-t2)*xscale, (float)h*0.25f, 1, "%12.6lf", t2);
				//}
				for(k=fps_start;;)//scan forward in time
				{
					ProfileEvent *e, *e2;
					double t1, t2;
					int k2;

					e=fps_checkpoints+(k&FPS_QUEUEMASK);
					if(e->start)
					{
						t2=e->t;
						k2=(k+1)&FPS_QUEUEMASK;
						while(k2!=fps_start)//search future for event end
						{
							e2=fps_checkpoints+(k2&FPS_QUEUEMASK);
							if(e2->event==e->event)
								break;
							k2=(k2+1)&FPS_QUEUEMASK;
						}
						if(k2==fps_start)//didn't finish yet
							t1=now;
						else
							t1=e2->t;
						//draw_line((float)(now-t1)*xscale
						//	, (float)(e->event+0)*yscale
						//	, (float)(now-t2)*xscale
						//	, (float)(e->event+1)*yscale
						//	, fpsprof_colors[e->event]
						//);
						draw_line((float)(now-t1)*xscale
							, (float)(e->event+1)*yscale
							, (float)(now-t2)*xscale
							, (float)(e->event+0)*yscale
							, fpsprof_colors[e->event]
						);
						//draw_rect((float)(now-t1)*xscale
						//	, (float)(now-t2)*xscale
						//	, (float)(e->event+0)*yscale
						//	, (float)(e->event+1)*yscale
						//	, fpsprof_colors[e->event]
						//);
					}
					k=(k+1)&FPS_QUEUEMASK;
					if(k==fps_start)//finished all events
						break;
				}
#if 0
				float graphstart=0.9f*(float)h;
				float graphscale=10.f*(float)h;
				double prev=0;
				double dsum=0;
				int dcount=0;

				prev=fps_checkpoints[fps_start&(FPS_QUEUESIZE-1)][0]=time_sec();
				fps_checkpoints[fps_start&(FPS_QUEUESIZE-1)][1]=trender;
				fps_checkpoints[fps_start&(FPS_QUEUESIZE-1)][2]=tdec;
				fps_checkpoints[fps_start&(FPS_QUEUESIZE-1)][3]=tsend2gpu;
				tdec=0;
				fps_start=(fps_start+1)&(FPS_QUEUESIZE-1);
			//	draw_curve_enqueue(&vertices_2d, 0, (float)h*0.75f-graphscale*0.1f);
			//	draw_curve_enqueue(&vertices_2d, (float)w, (float)h*0.75f-graphscale*0.1f);
			//	GUIPrint((float)w*0.5f, (float)w*0.5f, (float)h*0.75f-graphscale*0.1f, 2, "100 ms (10 FPS)");
				for(int k=0;k<FPS_QUEUESIZE&&k<w;++k)
				{
					double t;
					float y;

					t=fps_checkpoints[(k+fps_start)&(FPS_QUEUESIZE-1)][0];
					y=graphscale*(float)(t-prev);
					if(t>prev)
					{
						dsum+=y;
						++dcount;
					}
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+0, graphstart-y);
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+0, graphstart);
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+1, graphstart-y);
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+1, graphstart);
					prev=t;
				}
				draw_2d_flush(vertices_2d, 0xC0FFC080, GL_LINES);

				for(int k=0;k<FPS_QUEUESIZE&&k<w;++k)
				{
					double t;
					float y;

					t=fps_checkpoints[(k+fps_start)&(FPS_QUEUESIZE-1)][1];
					y=graphscale*(float)t;
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+0, graphstart-y);
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+0, graphstart);
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+1, graphstart-y);
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+1, graphstart);
				}
				draw_2d_flush(vertices_2d, 0xC080FFC0, GL_LINES);

				for(int k=0;k<FPS_QUEUESIZE&&k<w;++k)
				{
					double t;
					float y;

					t=fps_checkpoints[(k+fps_start)&(FPS_QUEUESIZE-1)][3];
					y=graphscale*(float)t;
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+0, graphstart-y);
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+0, graphstart);
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+1, graphstart-y);
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+1, graphstart);
					//if(t>100)
					//	GUIPrint(0
					//		, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+1
					//		, graphstart
					//		, 1
					//		, "%lf"
					//		, t
					//	);
				}
				draw_2d_flush(vertices_2d, 0xC080C0FF, GL_LINES);

				for(int k=0;k<FPS_QUEUESIZE&&k<w;++k)
				{
					double t;
					float y;

					t=fps_checkpoints[(k+fps_start)&(FPS_QUEUESIZE-1)][2];
					y=graphscale*(float)t;
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+0, graphstart-y);
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+0, graphstart);
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+1, graphstart-y);
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+1, graphstart);
					//if(t>100)
					//	GUIPrint(0
					//		, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+1
					//		, graphstart
					//		, 1
					//		, "%lf"
					//		, t
					//	);
				}
				draw_2d_flush(vertices_2d, 0xC0000000, GL_LINES);
				if(dcount)
					dsum/=dcount;
			//	draw_line(0.25f*(float)w, graphstart-graphscale*trender, 0.75f*(float)w, graphstart-graphscale*trender, 0xC0FF00FF);
				draw_line(0.25f*(float)w, graphstart-graphscale/100, 0.75f*(float)w, graphstart-graphscale/100, 0xC080C0FF);
				draw_line(0.25f*(float)w, graphstart-graphscale/ 50, 0.75f*(float)w, graphstart-graphscale/ 50, 0xC080C0FF);
				draw_line(0.25f*(float)w, graphstart-graphscale/ 20, 0.75f*(float)w, graphstart-graphscale/ 20, 0xC080C0FF);
			//	draw_line(0.25f*(float)w, graphstart-graphscale/ 10, 0.75f*(float)w, graphstart-graphscale/ 10, 0xC080C0FF);//10 fps = 100 ms
				GUIPrint(0, 0.75f*(float)w, graphstart-graphscale/(float)100, 1, "%12.3lf ms", 1000/(float)100);
				GUIPrint(0, 0.75f*(float)w, graphstart-graphscale/(float) 50, 1, "%12.3lf ms", 1000/(float) 50);
				GUIPrint(0, 0.75f*(float)w, graphstart-graphscale/(float) 20, 1, "%12.3lf ms", 1000/(float) 20);
			//	GUIPrint(0, 0.75f*(float)w, graphstart-graphscale/(float) 10, 1, "%12.3lf ms", 1000/(float) 10);//10 fps = 100 ms
				static int prevdrop=0;
				GUIPrint(0, 0, graphstart-(float)dsum, 1, "%12.3lf ms  %d/%10d/%10d", 1000*(dsum/graphscale), g_droppedframes-prevdrop, g_droppedframes, framenumber);
				prevdrop=g_droppedframes;
#endif
			}
#endif
#if defined PROFILE_FPS &&0
			if(anim_uitype==ANIMUI_STATS)
			{
				float graphstart=0.9f*(float)h;
				float graphscale=10.f*(float)h;
				double prev=0;
				double dsum=0;
				int dcount=0;

				prev=fps_checkpoints[fps_start&(FPS_QUEUESIZE-1)][0]=time_sec();
				fps_checkpoints[fps_start&(FPS_QUEUESIZE-1)][1]=trender;
				fps_checkpoints[fps_start&(FPS_QUEUESIZE-1)][2]=tdec;
				fps_checkpoints[fps_start&(FPS_QUEUESIZE-1)][3]=tsend2gpu;
				tdec=0;
				fps_start=(fps_start+1)&(FPS_QUEUESIZE-1);
			//	draw_curve_enqueue(&vertices_2d, 0, (float)h*0.75f-graphscale*0.1f);
			//	draw_curve_enqueue(&vertices_2d, (float)w, (float)h*0.75f-graphscale*0.1f);
			//	GUIPrint((float)w*0.5f, (float)w*0.5f, (float)h*0.75f-graphscale*0.1f, 2, "100 ms (10 FPS)");
				for(int k=0;k<FPS_QUEUESIZE&&k<w;++k)
				{
					double t;
					float y;

					t=fps_checkpoints[(k+fps_start)&(FPS_QUEUESIZE-1)][0];
					y=graphscale*(float)(t-prev);
					if(t>prev)
					{
						dsum+=y;
						++dcount;
					}
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+0, graphstart-y);
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+0, graphstart);
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+1, graphstart-y);
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+1, graphstart);
					prev=t;
				}
				draw_2d_flush(vertices_2d, 0xC0FFC080, GL_LINES);

				for(int k=0;k<FPS_QUEUESIZE&&k<w;++k)
				{
					double t;
					float y;

					t=fps_checkpoints[(k+fps_start)&(FPS_QUEUESIZE-1)][1];
					y=graphscale*(float)t;
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+0, graphstart-y);
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+0, graphstart);
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+1, graphstart-y);
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+1, graphstart);
				}
				draw_2d_flush(vertices_2d, 0xC080FFC0, GL_LINES);

				for(int k=0;k<FPS_QUEUESIZE&&k<w;++k)
				{
					double t;
					float y;

					t=fps_checkpoints[(k+fps_start)&(FPS_QUEUESIZE-1)][3];
					y=graphscale*(float)t;
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+0, graphstart-y);
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+0, graphstart);
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+1, graphstart-y);
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+1, graphstart);
					//if(t>100)
					//	GUIPrint(0
					//		, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+1
					//		, graphstart
					//		, 1
					//		, "%lf"
					//		, t
					//	);
				}
				draw_2d_flush(vertices_2d, 0xC080C0FF, GL_LINES);

				for(int k=0;k<FPS_QUEUESIZE&&k<w;++k)
				{
					double t;
					float y;

					t=fps_checkpoints[(k+fps_start)&(FPS_QUEUESIZE-1)][2];
					y=graphscale*(float)t;
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+0, graphstart-y);
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+0, graphstart);
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+1, graphstart-y);
					draw_curve_enqueue(&vertices_2d, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+1, graphstart);
					//if(t>100)
					//	GUIPrint(0
					//		, 0.5f*(float)(w-2*FPS_QUEUESIZE)+2*(float)(FPS_QUEUESIZE-1-k)+1
					//		, graphstart
					//		, 1
					//		, "%lf"
					//		, t
					//	);
				}
				draw_2d_flush(vertices_2d, 0xC0000000, GL_LINES);
				if(dcount)
					dsum/=dcount;
			//	draw_line(0.25f*(float)w, graphstart-graphscale*trender, 0.75f*(float)w, graphstart-graphscale*trender, 0xC0FF00FF);
				draw_line(0.25f*(float)w, graphstart-graphscale/100, 0.75f*(float)w, graphstart-graphscale/100, 0xC080C0FF);
				draw_line(0.25f*(float)w, graphstart-graphscale/ 50, 0.75f*(float)w, graphstart-graphscale/ 50, 0xC080C0FF);
				draw_line(0.25f*(float)w, graphstart-graphscale/ 20, 0.75f*(float)w, graphstart-graphscale/ 20, 0xC080C0FF);
			//	draw_line(0.25f*(float)w, graphstart-graphscale/ 10, 0.75f*(float)w, graphstart-graphscale/ 10, 0xC080C0FF);//10 fps = 100 ms
				GUIPrint(0, 0.75f*(float)w, graphstart-graphscale/(float)100, 1, "%12.3lf ms", 1000/(float)100);
				GUIPrint(0, 0.75f*(float)w, graphstart-graphscale/(float) 50, 1, "%12.3lf ms", 1000/(float) 50);
				GUIPrint(0, 0.75f*(float)w, graphstart-graphscale/(float) 20, 1, "%12.3lf ms", 1000/(float) 20);
			//	GUIPrint(0, 0.75f*(float)w, graphstart-graphscale/(float) 10, 1, "%12.3lf ms", 1000/(float) 10);//10 fps = 100 ms
				static int prevdrop=0;
				GUIPrint(0, 0, graphstart-(float)dsum, 1, "%12.3lf ms  %d/%10d/%10d", 1000*(dsum/graphscale), g_droppedframes-prevdrop, g_droppedframes, framenumber);
				prevdrop=g_droppedframes;
			}
#if 0
			{
				double tmin=HUGE_VAL, tmax=0;
				int nextidx=(fps_start-1)&(FPS_QUEUESIZE-1);
				fps_checkpoints[nextidx]=time_sec()-fps_checkpoints[fps_start];
				fps_start=nextidx;
				for(int k=0;k<FPS_QUEUESIZE;++k)
				{
					double t=fps_checkpoints[k];
					if(tmin>t)tmin=t;
					if(tmax<t)tmax=t;
				}
				if(tmax>tmin)
				{
					float x=0, y=0;
					float norm, norm2;

					tmax-=tmin;
					norm=16*(float)w/(float)tmax;
					norm2=1.f/(float)w;
					for(int k=0;k<FPS_QUEUESIZE;++k)
					{
						double t=fps_checkpoints[(k+fps_start)&(FPS_QUEUESIZE-1)];
						float x=((float)t-(float)tmin)*norm;
						y=floorf(x*norm2)*((float)h*(1.f/16));
						x=fmodf(x, (float)w);
						draw_curve_enqueue(&vertices_2d, x, y);
						draw_curve_enqueue(&vertices_2d, x, y+(float)h*(1.f/16));
					//	GUIPrint(x, x, (float)h*0.5f+x, 1, "%d", framenumber);
					}
					draw_2d_flush(vertices_2d, 0xFFFF00FF, GL_LINES);
				}
			}
#endif
#endif
		}
		if(impreview&&profileplotmode>PROFILE_OFF)
		{
			switch(imagetype)
			{
			default://make gcc happy
				break;
			case IM_GRAYSCALEv2:
				draw_profile(0, 0xFF000000, profileplotmode==PROFILE_X, pixelpreview);
				break;
			case IM_RGBA:
				draw_profile(0, 0xFF0000FF, profileplotmode==PROFILE_X, pixelpreview);
				draw_profile(1, 0xFF00FF00, profileplotmode==PROFILE_X, pixelpreview);
				draw_profile(2, 0xFFFF0000, profileplotmode==PROFILE_X, pixelpreview);
				draw_profile(3, 0xFF000000, profileplotmode==PROFILE_X, pixelpreview);
				break;
			case IM_BAYERv2:
				draw_profile(0, 0xFF000000|0xFF<<(bayer[0]<<3), profileplotmode==PROFILE_X, pixelpreview);
				draw_profile(1, 0xFF000000|0xFF<<(bayer[1]<<3), profileplotmode==PROFILE_X, pixelpreview);
				draw_profile(2, 0xFF000000|0xFF<<(bayer[2]<<3), profileplotmode==PROFILE_X, pixelpreview);
				draw_profile(3, 0xFF000000|0xFF<<(bayer[3]<<3), profileplotmode==PROFILE_X, pixelpreview);
				break;
			}
#if 0
			void (*draw_profile)(int comp, int color);
			if(pixelpreview)
			{
				draw_profile=profileplotmode==PROFILE_X?draw_profile_x_preview:draw_profile_y_preview;
				draw_profile(0, 0xFF0000FF);
				draw_profile(1, 0xFF00FF00);
				draw_profile(2, 0xFFFF0000);
				draw_profile(3, 0xFF000000);
			}
			else
			{
				draw_profile=profileplotmode==PROFILE_X?draw_profile_x:draw_profile_y;
				switch(imagetype)
				{
				default://make gcc happy
					break;
				case IM_GRAYSCALEv2:
					draw_profile(0, 0xFF000000);
					break;
				case IM_RGBA:
					draw_profile(0, 0xFF0000FF);
					draw_profile(1, 0xFF00FF00);
					draw_profile(2, 0xFFFF0000);
					draw_profile(3, 0xFF000000);
					break;
				case IM_BAYERv2:
					draw_profile(0, 0xFF000000|0xFF<<(bayer[0]<<3));
					draw_profile(1, 0xFF000000|0xFF<<(bayer[1]<<3));
					draw_profile(2, 0xFF000000|0xFF<<(bayer[2]<<3));
					draw_profile(3, 0xFF000000|0xFF<<(bayer[3]<<3));
					break;
				}
			}
#endif
		}

		if(impreview&&image)
		{
			const char *imtypestr="?";
			switch(imagetype)
			{
			default://make gcc happy
				break;
			case IM_UNINITIALIZED:	imtypestr="IM_UNINITIALIZED";	break;
			case IM_GRAYSCALEv2:	imtypestr="IM_GRAYSCALEv2";	break;
			case IM_RGBA:		imtypestr="IM_RGBA";		break;
			case IM_BAYERv2:	imtypestr="IM_BAYERv2";		break;
			}
			//g_printed=0;
			GUIPrint_append(0, 0, h-tdy, 1, 0, "%s  XY(%5d, %5d) / WH%5d x%5d  x%lf bright%d  %s  depth %d  %10td bytes %10.6lf:1",
				strlastmodified,
				imx, imy, impreview->iw, impreview->ih,
				zoom,
				brightness,
				imtypestr,
				imagedepth,
				filesize,
				(double)image->nch*image->iw*image->ih*log2(image->nlevels0)/(8*filesize)
			);
			if(bitmode==2)
				GUIPrint_append(0, 0, h-tdy, 1, 0, "  Bitplane  %2d", bitplane);
			else if(bitmode==1)
				GUIPrint_append(0, 0, h-tdy, 1, 0, "  Bitplane%d:%2d", bitplane/imagedepth, bitplane%imagedepth);
		}
		if(animated)
		{
			if(impreview)
				GUIPrint_append(0, 0, h-tdy, 1, 0, " F%12lld", framenumber);//FIXME doesn't support seeking
			//else
			//	GUIPrint_append(0, 0, h-tdy, 1, 0, " %10.3lf sec", slider.timestamp);
#ifdef _DEBUG
			extern double vtime, atime;
			GUIPrint_append(0, 0, h-tdy, 1, 0, "  V %12.6lf  A %12.6lf", vtime, atime);
#endif
		}
		if(impreview&&image)
		{
			if((unsigned)imx<(unsigned)impreview->iw&&(unsigned)imy<(unsigned)impreview->ih)
			{
				unsigned char *p=impreview->data+((ptrdiff_t)impreview->iw*imy+imx)*impreview->nch;
				unsigned short *p0=(unsigned short*)image->data+((ptrdiff_t)image->iw*imy+imx)*image->nch;
				switch(imagetype)
				{
				default://make gcc happy
					break;
				case IM_GRAYSCALEv2:
					if(has_alpha)
						GUIPrint_append(0, 0, h-tdy, 1, 0, "  GRAY_ALPHA(%3d, %3d)=0x%04X%04X",
							(unsigned)p[0], (unsigned)p[3],
							(unsigned)p0[0], (unsigned)p0[3]
						);
					else
						GUIPrint_append(0, 0, h-tdy, 1, 0, "  GRAY(%3d)=0x%04X", (unsigned)p[0], (unsigned)p0[0]);
					break;
				case IM_RGBA:
					{
						long long color;
						memcpy(&color, p0, sizeof(color));
						GUIPrint_append(0, 0, h-tdy, 1, 0, "  RGBA(%3d, %3d, %3d, %3d)=0x%016llX",
							(unsigned)p[0],
							(unsigned)p[1],
							(unsigned)p[2],
							(unsigned)p[3],
							color
						);
					}
					break;
				case IM_BAYERv2:
					{
						const char labels[]="RGB";
						int comp=(imy&1)<<1|(imx&1);
						GUIPrint_append(0, 0, h-tdy, 1, 0, "  %c%c%c%c  %c(%5d/%5d)=0x%04X",
							labels[(int)bayer[0]],
							labels[(int)bayer[1]],
							labels[(int)bayer[2]],
							labels[(int)bayer[3]],
							labels[(int)bayer[comp]],
							(unsigned)p0[(int)bayer[comp]], image->nlevels0,
							(unsigned)p0[(int)bayer[comp]]
						);
					}
					break;
				}
			}
		}
		GUIPrint_append(0, 0, h-tdy, 1, 1, "");
		//if((unsigned)imx<(unsigned)impreview->iw&&(unsigned)imy<(unsigned)impreview->ih)
		//{
		//	unsigned char *p=impreview->data+((impreview->iw*imy+imx)<<2);
		//	int color;
		//	memcpy(&color, p, sizeof(color));
		//	GUIPrint(0, 0, h-tdy, 1, "XY(%d, %d) / WH %dx%d  x%lf  %s  %d bit  RGBA(%3d, %3d, %3d, %3d)=0x%08X", imx, imy, impreview->iw, impreview->ih, zoom, imtypestr, imagedepth, p[0], p[1], p[2], p[3], color);
		//}
		//else
		//	GUIPrint(0, 0, h-tdy, 1, "XY(%d, %d) / WH %dx%d  x%lf  %s  %d bit", imx, imy, impreview->iw, impreview->ih, zoom, imtypestr, imagedepth);
	}
	//extern int mouse_bypass;
	static double t=0;
	double t2=time_sec();
	GUIPrint(0, 0, 0, 1, "fps %lf", t2?1/(t2-t):0);
	//GUIPrint(0, 0, 0, 1, "fps %lf  T %lf ms",
	//	1000./(t2-t), (t2-t)*0.001
	//);
	//GUIPrint(0, 0, 0, 1, "fps %lf  T%10.2lf 2D%10.2lf MB",
	//	1000./(t2-t),
	//	vertices_text?(double)(vertices_text->cap)/(1024.*1024):0,
	//	vertices_2d?(double)(vertices_2d->cap)/(1024.*1024):0
	//);
	t=t2;
	swapbuffers();
#ifdef PROFILE_FPS
	recordevent(EVNT_RENDER, 0);
	//trender_current=time_sec()-trender_current;
	//trender=trender_current;
#endif
}
int io_quit_request()//return 1 to exit
{
	return 1;
}
void io_cleanup()//cleanup
{
	image_free(&image);
	image_free(&impreview);
}
