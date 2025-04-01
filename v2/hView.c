#include"hView.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<math.h>
#include<sys/stat.h>
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
} Drag;
int drag=DRAG_NONE,
	start_mx=0, start_my=0;//initial drag mouse coords

int imagecentered=0;
double zoom=1;//image pixel size in screen pixels
#define ZOOM_LIMIT_LABEL 48
#define ZOOM_LIMIT_ALPHA 96
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

static void center_image()
{
	if(!impreview)
		return;
	int wndw=w, wndh=h-17;
	if((double)wndw/wndh>=(double)impreview->iw/impreview->ih)//window AR > image AR: fit height
	{
		if(wndh>0)
			zoom=(double)wndh/impreview->ih;
	}
	else//window AR < image AR: fit width
		zoom=(double)wndw/impreview->iw;
	wpx=(impreview->iw-wndw/zoom)*0.5;//center image
	wpy=(impreview->ih-wndh/zoom)*0.5;
	imagecentered=1;
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
}
static void update_image(int settitle, int render)
{
	if(!image)
		return;
	if(settitle)
		set_window_title("%s - hView", (char*)fn->data);
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
	if(imagecentered)
		center_image();
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

	imagecentered=0;
}
int io_init(int argc, char **argv)//return false to abort
{
#ifdef _DEBUG
	fn=filter_path(
	//	"E:/Share Box/20230102 dslr/NEF/DSC_0403.NEF"
	//	"D:/ML/20250320_005230.dng"
	//	"D:/ML/dataset-RAW/a0001-jmac_DSC1459.dng"
	//	"D:/ML/dataset-RAW/a0117-kme_006.dng"
		"D:/ML/dataset-RAW/a0118-20051223_103622__MG_0617.dng"
	//	"D:/ML/dataset-RAW/a0128-IMG_0793.dng"
	//	"D:/ML/00-Taschdid.svg.png"
	//	"D:/ML/mystery.gr"
	//	"D:/ML/kodim24.ppm"
	//	"D:/ML/mystery.gr"
	//	"E:/C/ASCII-Table-wide.svg.png"
	//	"C:/dataset-20241107-gr/20241107_164228_958.gr"
	//	"E:/C/huf2gr/huf2gr/plane.gr"
	//	"E:/Share Box/Scope/20241107/20241107_164651_573.huf"
	//	"E:/Share Box/Scope/20241107/20241107_164228_958.huf"
	//	"C:/Projects/datasets/dataset-RAW/6K9A8788.CR3"
		, -1, 0);
	load_media((char*)fn->data, &image, 1);
	if(image)
		update_image(1, 0);
	else
		array_free(&fn);
#else
	if(argc>0)
	{
		fn=filter_path(argv[0], -1, 0);
		load_media((char*)fn->data, &image, 1);
		if(image)
			update_image(1, 0);
		else
			array_free(&fn);
	}
#endif
	if(!image)
		set_window_title("hView");
	return 1;
}
void io_resize()
{
	if(image&&image->iw&&image->ih&&imagecentered)
		center_image();
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
	return !timer;//
}
int io_mousewheel(int forward)
{
	int mw_fwd=forward>0;
	if(GET_KEY_STATE(KEY_CTRL))
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
				unsigned short *data=image->data;
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
				for(kdot=(int)fn->count-1;kdot>=0&&fn->data[kdot]!='.';--kdot);
				kslash=kdot-1;
				for(kslash=kdot-1;kslash>=0&&fn->data[kslash]!='/'&&fn->data[kslash]!='\\';--kslash);
				++kslash;
				save_media_as(image, impreview, (char*)fn->data+kslash, kdot-kslash, 1);
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
			char *ver=get_codecinfo();
			messagebox(MBOX_OK, "Controls",
				"Esc/LBUTTON/WASD: Drag image\n"
				"Enter/Bksp/Wheel: Zoom image\n"
				"E: Reset view to topleft corner at 1:1\n"
				"C: Fit image to window\n"
				"+/-: Change brightness\n"
				"Q: Equalize histogram\n"
				"Left/Right: prev/next image\n"
				"Ctrl O: Open image\n"
				"R/F5: Refresh\n"
				"Ctrl X/Y/R/T: Flip/rotate/transpose\n"
				"Ctrl S: Save as\n"
				"Ctrl A: Set alpha to 1\n"
				"Ctrl C: Copy pixel values from screen (when zoomed in)\n"
				"Ctrl V: Paste bitmap from clipboard\n"
				"H: Toggle histogram\n"
				"Ctrl H: Toggle hexadecimal pixel labels\n"
				"P: Toggle pixel label source\n"
				"X/Y: Toggle horizontal/vertical cross-section profiles\n"
				"\n"
				"Quote: Toggle bitplane view\n"
				"Brackets: Select bitplane\n"
				"%s %s\n"
				"%s"
				, __DATE__, __TIME__
				, ver?ver:""
			);
			free(ver);
		}
		break;
	case KEY_F2://info
		{
			char buf[1024]={0};
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
				struct stat info={0};
				int e2=stat((char*)fn->data, &info);
				if(e2)
					nprinted+=snprintf(buf+nprinted, sizeof(buf)-1-nprinted,
						"\"%s\"\n"
						"INACCESSIBLE\n"
						, (char*)fn->data
					);
				else
				{
					ptrdiff_t csize=info.st_size;
				//	ptrdiff_t csize=get_filesize((char*)fn->data);
					created2=info.st_ctime;
					lastmodified2=info.st_mtime;
					lastaccess2=info.st_atime;
					get_filetitle((char*)fn->data, (int)fn->count, 0, &extidx);
					nprinted+=snprintf(buf+nprinted, sizeof(buf)-1-nprinted,
						"\"%s\"\n"
						"%11td bytes ("
						, (char*)fn->data
						, usize
					);
					nprinted+=print_memsize(buf+nprinted, sizeof(buf)-1-nprinted, usize, 8);
					nprinted+=snprintf(buf+nprinted, sizeof(buf)-1-nprinted,
						") bitmap\n"
						"%11td bytes ("
						, csize
					);
					nprinted+=print_memsize(buf+nprinted, sizeof(buf)-1-nprinted, csize, 8);
					nprinted+=snprintf(buf+nprinted, sizeof(buf)-1-nprinted,
						") %s\n"
						, (char*)fn->data+extidx
					);
				}
			}
			else
			{
				nprinted+=snprintf(buf+nprinted, sizeof(buf)-1-nprinted,
					"%10td bytes ("
					, usize
				);
				nprinted+=print_memsize(buf+nprinted, sizeof(buf)-1-nprinted, usize, 8);
				nprinted+=snprintf(buf+nprinted, sizeof(buf)-1-nprinted, ") bitmap\n");
			}
			switch(imagetype)
			{
			default://make gcc happy
				break;
			case IM_GRAYSCALEv2:
				nprinted+=snprintf(buf+nprinted, sizeof(buf)-1-nprinted,
					"CWH %d*%5d*%5d G\n"
					, image->nch, image->iw, image->ih
				);
				break;
			case IM_RGBA:
				if(image->srcnch>=3)
					nprinted+=snprintf(buf+nprinted, sizeof(buf)-1-nprinted,
						"CWH %d*%5d*%5d RGB%s\n"
						, image->nch, image->iw, image->ih, image->srcnch==4?"A":""
					);
				else
					nprinted+=snprintf(buf+nprinted, sizeof(buf)-1-nprinted,
						"CWH %d*%5d*%5d G%s\n"
						, image->nch, image->iw, image->ih, image->srcnch==2?"A":""
					);
				break;
			case IM_BAYERv2:
				{
					char labels[]="RGB";
					nprinted+=snprintf(buf+nprinted, sizeof(buf)-1-nprinted,
						" WH   %5d*%5d %c%c%c%c\n"
						, image->iw, image->ih
						, labels[(int)bayer[0]]
						, labels[(int)bayer[1]]
						, labels[(int)bayer[2]]
						, labels[(int)bayer[3]]
					);
				}
				break;
			}
			nprinted+=snprintf(buf+nprinted, sizeof(buf)-1-nprinted,
				"CWH %d*%5d*%5d preview\n"
				, impreview->nch, impreview->iw, impreview->ih
			);
			if(created2)
			{
				struct tm date={0};
				localtime_s(&date, &created2);
				nprinted+=(int)strftime(buf+nprinted, sizeof(buf)-1-nprinted, "%Y-%m-%d_%H%M%S Created\n", &date);
				localtime_s(&date, &lastmodified2);
				nprinted+=(int)strftime(buf+nprinted, sizeof(buf)-1-nprinted, "%Y-%m-%d_%H%M%S Modified\n", &date);
				localtime_s(&date, &lastaccess2);
				nprinted+=(int)strftime(buf+nprinted, sizeof(buf)-1-nprinted, "%Y-%m-%d_%H%M%S Accessed\n", &date);
			}

			int cancel=messagebox(MBOX_OKCANCEL, "Copy to clipboard?", "%s", buf);
			if(!cancel)
				copy_to_clipboard(buf, nprinted);
		}
		break;
	case KEY_ESC:
	case KEY_LBUTTON:
		if(drag==DRAG_NONE)//start dragging
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
			STR_COPY(path, fn->data, fn->count);
			//acme_strrchr((char*)path->data, path->count, '/');//X  what about backslash?
			for(int k=(int)path->count-1;k>=0;--k)
			{
				if(path->data[k]=='/'||path->data[k]=='\\')
				{
					path->data[k+1]=0;
					path->count=k+1;
					break;
				}
			}
			ArrayHandle filenames=get_filenames((char*)path->data, 0, 0, 1), *fn2;
			if(filenames&&filenames->count)
			{
				array_free(&path);
				int currentidx=-1;
				for(int k=0;k<(int)filenames->count;++k)
				{
					fn2=array_at(&filenames, k);
					if(!strcmp((char*)fn2[0]->data, (char*)fn->data))
					{
						currentidx=k;
						break;
					}
				}
				Image16 *im2=0;
				int step=key==KEY_RIGHT?1:-1;
				for(int k=currentidx+step;MODVAR(k, k, (int)filenames->count), k!=currentidx;k+=step)
				{
					fn2=array_at(&filenames, k);
					if(!load_media((char*)fn2[0]->data, &im2, 0))
					{
						currentidx=k;
						break;
					}
					if(im2)
						image_free(&im2);
				}
				if(im2)
				{
					image_free(&image);
					image=im2;
					array_free(&fn);
					fn=filter_path((char*)fn2[0]->data, -1, 0);
					update_image(1, 1);
				}
				array_free(&filenames);
				return 1;
			}
		}
		break;
	case 'O':
		if(GET_KEY_STATE(KEY_CTRL))
		{
			ArrayHandle fn2=dialog_open_file(0, 0, 0);
			if(fn2)
			{
				Image16 *im2=0;
				int error=load_media((char*)fn2->data, &im2, 1);
				if(im2&&!error)
				{
					image_free(&image);
					image=im2;
					if(fn)
						array_free(&fn);
					fn=filter_path((char*)fn2->data, -1, 0);
					update_image(1, 0);
				}
				array_free(&fn2);
				return image!=0;
			}
		}
		break;
	case 'Q'://equalization
		if(GET_KEY_STATE(KEY_CTRL))
			update_image(0, 0);
		else
			equalize();
		if(hist_on)
			calc_hist();
		return 1;
	case 'E':
		wpx=0, wpy=0, zoom=1;
		imagecentered=0;
		return 1;
	case 'C':
		if(GET_KEY_STATE(KEY_CTRL))//copy screen contents
		{
			if(!image||!impreview)
				break;
			if(zoom<ZOOM_LIMIT_LABEL)
			{
				int
					x1=screen2image_x_int_rounded(0), y1=screen2image_y_int_rounded(0),
					x2=screen2image_x_int_rounded(w), y2=screen2image_y_int_rounded(h);
				if(x1<0)x1=0;
				if(y1<0)y1=0;
				if(x2>impreview->iw)x2=impreview->iw;
				if(y2>impreview->ih)y2=impreview->ih;
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
				int
					sx1=image2screen_x_int(0), sx2=image2screen_x_int(impreview->iw),
					sy1=image2screen_y_int(0), sy2=image2screen_y_int(impreview->ih);
				int csx1=sx1, csx2=sx2;
				int csy1=sy1, csy2=sy2;
				CLAMP2(csx1, 0, w);
				CLAMP2(csx2, 0, w);
				CLAMP2(csy1, 0, h);
				CLAMP2(csy2, 0, h);
				int ix1=screen2image_x_int(csx1), ix2=screen2image_x_int(csx2);
				int iy1=screen2image_y_int(csy1), iy2=screen2image_y_int(csy2);
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
		else
			center_image();
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
				for(ptrdiff_t k=0, res=(ptrdiff_t)im2->iw*im2->ih;k<res;++k)
					image->data[k]=im2->data[k];
			//	image=image_construct(im2->iw, im2->ih, 16, im2->data, im2->iw, im2->ih, im2->xcap-im2->iw, im2->depth);
				impreview=im2;
				imagetype=IM_RGBA;
				if(imagecentered)
					center_image();
				return 1;
			}
		}
		break;
	case 'P':
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
			load_media((char*)fn->data, &im2, 0);
			if(im2)
			{
				image_free(&image);
				image=im2;
				update_image(0, 1);
			}
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
		if(drag==DRAG_IMAGE)//stop dragging
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
}
static void print_pixellabels(int ix1, int ix2, int iy1, int iy2, int xoffset, int yoffset, int component, char label, long long txtcolors, int is_bayer, int tight)
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
		int ky=image2screen_y_int(y);
		int ix=MAXVAR(ix1, 0), xend=MINVAR(ix2+2, image->iw);
		for(;ix<xend;++ix)
		{
			float x=(float)(ix+xoffset);
			int kx=image2screen_x_int(x);
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
static void print_pixellabels_preview(int ix1, int ix2, int iy1, int iy2, int xoffset, int yoffset, int component, char label, long long txtcolors, int is_bayer, int tight)
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
		int ky=image2screen_y_int(iy+yoffset);
		int ix=MAXVAR(ix1, 0), xend=MINVAR(ix2+2, impreview->iw);
		ix>>=is_bayer;
		ix<<=is_bayer;
		for(;ix<xend;ix+=1<<is_bayer)
		{
			int kx=image2screen_x_int(ix+xoffset);
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
static void draw_profile_x(int comp, int color)//horizontal cross-section profile		to see the color/spatial correlation
{
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
}
static void draw_profile_y(int comp, int color)//vertical cross-section profile
{
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
void io_render()
{
	if(h<=0)
		return;
	if(!background[3])
	{
		background[3]=255;
		glClearColor(background[0]/255.f, background[1]/255.f, background[2]/255.f, 1);
	}
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

	if(impreview)
	{
		int
			sx1=image2screen_x_int(0), sx2=image2screen_x_int(impreview->iw),
			sy1=image2screen_y_int(0), sy2=image2screen_y_int(impreview->ih);
		display_texture_i(sx1, sx2, sy1, sy2, (int*)impreview->data, impreview->iw, impreview->ih, 0, 1, 0, 1, 1, 0);
		int
			imx=screen2image_x_int(mx),
			imy=screen2image_y_int(my);
		if(zoom>=ZOOM_LIMIT_LABEL)
		{
			int tight=zoom<ZOOM_LIMIT_LABEL*2;
			int csx1=sx1, csx2=sx2;
			int csy1=sy1, csy2=sy2;
			CLAMP2(csx1, 0, w);
			CLAMP2(csx2, 0, w);
			CLAMP2(csy1, 0, h);
			CLAMP2(csy2, 0, h);
			int ix1=screen2image_x_int(csx1), ix2=screen2image_x_int(csx2);
			int iy1=screen2image_y_int(csy1), iy2=screen2image_y_int(csy2);
			const char labels[]="rgb";
			long long theme[]=
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
					draw_histogram(histogram    , 256, 0x800000FF, y1       , y1+dy/3);
					draw_histogram(histogram+256, 256, 0x8000FF00, y1+dy  /3, y1+dy*2/3);
					draw_histogram(histogram+512, 256, 0x80FF0000, y1+dy*2/3, y2);
				}
				break;
			}
		}
		if(profileplotmode>PROFILE_OFF)
		{
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
		}
		
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
