#include"hView.h"
#include<stdlib.h>
#include<string.h>
#include<math.h>
static const char file[]=__FILE__;

//active keys turn on timer
#define ACTIVE_KEY_LIST\
	AK('W') AK('A') AK('S') AK('D')\
	AK(KEY_ENTER) AK(KEY_BKSP)\
	AK(KEY_UP) AK(KEY_DOWN)
//	AK(KEY_LEFT) AK(KEY_RIGHT) AK('T') AK('G')
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
ImageHandle
	image=0,//16bit
	impreview=0;//8bit
ImageType imagetype=IM_UNINITIALIZED;
int imagedepth=0;
char bayer[4]={0};//shift ammounts for the 4 Bayer mosaic components, -1 for grayscale, example: RGGB is {0, 8, 8, 16}
int debayer_on=1;
int has_alpha=0;
ptrdiff_t filesize=0;
double format_CR=0;
unsigned char background[]={0, 0, 0, 255};

static ArrayHandle vertices_text=0;
int pxlabels_hex=1;

int hist_on=0;
int histogram[768];//histogram of preview buffer which is always 8-bit
ArrayHandle vertices_2d=0;

ProfilePlotMode profileplotmode=PROFILE_OFF;

int bitmode=0,//0: off, 1: colorful bitplanes, 2: monochrome bitplanes
	bitplane=-1;

static void center_image()
{
	if(!image)
		return;
	int wndw=w, wndh=h-17;
	if((double)wndw/wndh>=(double)image->iw/image->ih)//window AR > image AR: fit height
	{
		if(wndh>0)
			zoom=(double)wndh/image->ih;
	}
	else//window AR < image AR: fit width
		zoom=(double)wndw/image->iw;
	wpx=(image->iw-wndw/zoom)*0.5;//center image
	wpy=(image->ih-wndh/zoom)*0.5;
	imagecentered=1;
}
static void calc_hist()
{
	if(!impreview)
		return;
	int res=impreview->iw*impreview->ih;
	memset(histogram, 0, 768*sizeof(int));
	switch(imagetype)
	{
	case IM_GRAYSCALE:
		for(int k=0;k<res;++k)
		{
			unsigned char sym=impreview->data[k<<2];
			++histogram[sym];
		}
		break;
	case IM_RGBA:
		for(int k=0;k<res;++k)
		{
			unsigned char *p=impreview->data+((size_t)k<<2);
			++histogram[0<<8|p[0]];
			++histogram[1<<8|p[1]];
			++histogram[2<<8|p[2]];
		}
		break;
	case IM_BAYER:
		for(int ky=0;ky<impreview->ih;++ky)
		{
			for(int kx=0;kx<impreview->iw;++kx)
			{
				int comp=bayer[(ky&1)<<1|kx&1];
				unsigned char sym=impreview->data[(impreview->iw*ky+kx)<<2|comp];
				++histogram[comp<<8|sym];
			}
		}
		break;
	}
}
static int integrate_hist(int *hist, int nlevels, int *CDF)
{
	int sum=0;
	for(int sym=0;sym<nlevels;++sym)
	{
		CDF[sym]=sum;
		sum+=hist[sym];
	}
	return sum;
}
static void equalize()
{
	if(!impreview)
		return;
	calc_hist();
	int *CDF=(int*)malloc(256*sizeof(int));
	if(!CDF)
	{
		LOG_ERROR("Allocation error");
		return;
	}
	int res=impreview->iw*impreview->ih, fsum;
	unsigned char *ptr=(unsigned char*)impreview->data;
	switch(imagetype)
	{
	case IM_GRAYSCALE:
		fsum=integrate_hist(histogram, 256, CDF);
		for(int k=0;k<res;++k)
		{
			int val=ptr[k<<2];
			val=(int)((long long)CDF[val]*255/fsum);
			ptr[k<<2  ]=val;
			ptr[k<<2|1]=val;
			ptr[k<<2|2]=val;
		}
		break;
	case IM_BAYER:
	case IM_RGBA:
		for(int kc=0;kc<3;++kc)
		{
			fsum=integrate_hist(histogram+((size_t)kc<<8), 256, CDF);
			for(int k=0;k<res;++k)
			{
				int val=ptr[k<<2|kc];
				val=(int)((long long)CDF[val]*255/fsum);
				ptr[k<<2|kc]=val;
			}
		}
		break;
	//case IM_BAYER:
	//	for(int kb=0;kb<4;++kb)
	//	{
	//		int xoffset=kb&1, yoffset=kb>>1, kc=bayer[kb];
	//		fsum=integrate_hist(histogram+((size_t)kc<<8), 256, CDF);
	//		for(int ky=yoffset;ky<impreview->ih;ky+=2)
	//		{
	//			for(int kx=xoffset;kx<impreview->iw;kx+=2)
	//			{
	//				int idx=impreview->iw*ky+kx;
	//				int val=ptr[idx<<2|kc];
	//				val=(int)((long long)CDF[val]*255/fsum);
	//				ptr[idx<<2|kc]=val;
	//			}
	//		}
	//	}
	//	break;
	}
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
		impreview=image_construct(0, 0, 8, 0, image->iw, image->ih, 0, image->depth);
	}
	if(bitmode==1)
	{
		const unsigned short *src=(const unsigned short*)image->data;
		unsigned char *dst=(unsigned char*)impreview->data;
		int sh=bitplane+16-imagedepth, srcidx, dstidx;
		for(int ky=0;ky<image->ih;++ky)
		{
			for(int kx=0;kx<image->iw;++kx)
			{
				srcidx=(image->iw*ky+kx)<<2, dstidx=(impreview->iw*ky+kx)<<2;
				for(int kc=0;kc<3;++kc)
					dst[dstidx|kc]=src[srcidx|kc]>>sh&1?255:0;
				dst[dstidx|3]=src[srcidx|3]>>8;//copy alpha
			}
		}
	}
	else if(bitmode==2)
	{
		const unsigned long long *src=(const unsigned long long*)image->data;
		unsigned char *dst=(unsigned char*)impreview->data;
		int kb=bitplane%imagedepth, kc=bitplane/imagedepth;
		int sh=(kc<<4)+kb+16-imagedepth, srcidx, dstidx;
		for(int ky=0;ky<image->ih;++ky)
		{
			for(int kx=0;kx<image->iw;++kx)
			{
				srcidx=image->iw*ky+kx, dstidx=(impreview->iw*ky+kx)<<2;
				int val=src[srcidx]>>sh&1?255:0;
				dst[dstidx]=val;
				dst[dstidx|1]=val;
				dst[dstidx|2]=val;
				dst[dstidx|3]=src[srcidx]>>(48+8);//copy alpha
			}
		}
	}
	else
		image_export_rgb8(impreview, image, imagetype);
	//	image_blit(impreview, 0, 0, image->data, image->iw, image->ih, image->xcap-image->iw, image->depth);
	if(hist_on)
		calc_hist();
	if(imagecentered)
		center_image();
	if(render)
		io_render();
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
	//	"D:/ML/mystery.gr"
		"E:/C/ASCII-Table-wide.svg.png"
	//	"C:/dataset-20241107-gr/20241107_164228_958.gr"
	//	"E:/C/huf2gr/huf2gr/plane.gr"
	//	"E:/Share Box/Scope/20241107/20241107_164651_573.huf"
	//	"E:/Share Box/Scope/20241107/20241107_164228_958.huf"
	//	"C:/Projects/datasets/dataset-RAW/6K9A8788.CR3"
		, 1);
	load_media((char*)fn->data, &image, 1);
	if(image)
		update_image(1, 0);
	else
		array_free(&fn);
#else
	if(argc>0)
	{
		fn=filter_path(argv[0], 1);
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
	const double tolerance=1e-2;
	int mw_fwd=forward>0;

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
		case 'A'://set alpha to one
			{
				unsigned short *data=(unsigned short*)image->data;
				ptrdiff_t res=image->iw*image->ih;
				for(ptrdiff_t k=0;k<res;++k)
					data[k<<2|3]=0xFFFF;
				for(ptrdiff_t k=0;k<res;++k)
					impreview->data[k<<2|3]=0xFF;
			}
		case 'S':
			{
				int kslash=0, kdot=0;
				for(kdot=(int)fn->count-1;kdot>=0&&fn->data[kdot]!='.';--kdot);
				kslash=kdot-1;
				for(kslash=kdot-1;kslash>=0&&fn->data[kslash]!='/'&&fn->data[kslash]!='\\';--kslash);
				++kslash;
				save_media_as(impreview, (char*)fn->data+kslash, kdot-kslash, 1);
			}
			return 1;
		}
	}
	switch(key)
	{
	case KEY_F1:
		messagebox(MBOX_OK, "Controls",
			"Esc/LBUTTON/WASD: Drag image\n"
			"Enter/Bksp/Wheel: Zoom image\n"
			"Left/Right: prev/next image\n"
			"Ctrl O: Open image\n"
			"E: Reset view to topleft corner at 1:1\n"
			"C: Fit image to window\n"
			"Q: Equalize histogram\n"
			"Ctrl C: Copy pixel values from screen (when zoomed in)\n"
			"H: Toggle histogram\n"
			"Ctrl H: Toggle hexadecimal pixel labels\n"
			"X/Y: Toggle horizontal/vertical cross-section profiles\n"
			"\n"
			"Quote: Toggle bitplane view\n"
			"Brackets: Select bitplane\n" 
		);
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
			ArrayHandle filenames=get_filenames(path->data, 0, 0, 1), *fn2;
			if(filenames&&filenames->count)
			{
				array_free(&path);
				int currentidx=-1;
				for(int k=0;k<(int)filenames->count;++k)
				{
					fn2=array_at(&filenames, k);
					if(!strcmp(fn2[0]->data, fn->data))
					{
						currentidx=k;
						break;
					}
				}
				ImageHandle im2=0;
				int step=key==KEY_RIGHT?1:-1;
				for(int k=currentidx+step;MODVAR(k, k, (int)filenames->count), k!=currentidx;k+=step)
				{
					fn2=array_at(&filenames, k);
					if(!load_media(fn2[0]->data, &im2, 0))
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
					fn=filter_path(fn2[0]->data, 1);
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
				ImageHandle im2=0;
				int error=load_media(fn2->data, &im2, 1);
				if(im2&&!error)
				{
					image_free(&image);
					image=im2;
					if(fn)
						array_free(&fn);
					fn=filter_path(fn2->data, 1);
					update_image(1, 0);
				}
				array_free(&fn2);
				return image!=0;
			}
		}
		break;
		
	case 'B':
		debayer_on=!debayer_on;
		update_image(0, 0);
		return 1;
	case 'Q'://equalization
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
				int x1=screen2image_x_int_rounded(0), y1=screen2image_y_int_rounded(0),
					x2=screen2image_x_int_rounded(w), y2=screen2image_y_int_rounded(h);
				if(x1<0)x1=0;
				if(y1<0)y1=0;
				if(x2>impreview->iw)x2=impreview->iw;
				if(y2>impreview->ih)y2=impreview->ih;
				int iw=x2-x1, ih=y2-y1;
				if(iw>0&&ih>0)
				{
					ImageHandle crop=image_construct(iw, ih, 8, impreview->data+((impreview->iw*(ptrdiff_t)y1+x1)<<2), iw, ih, impreview->xcap-iw, impreview->depth);
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
				case IM_GRAYSCALE:
					{
						str_append(&str, "%d bit GRAY:\n", imagedepth);
						unsigned short *ptr=(unsigned short*)image->data;
						int iy=MAXVAR(iy1, 0);
						for(int yend=MINVAR(iy2+2, image->ih);iy<yend;++iy)
						{
							int ky=image2screen_y_int(iy);
							int ix=MAXVAR(ix1, 0);
							for(int xend=MINVAR(ix2+2, image->iw);ix<xend;++ix)
							{
								int kx=image2screen_x_int(ix);
								int idx=(image->iw*iy+ix)<<2;
								str_append(&str, format, ptr[idx  ]>>(16-imagedepth));
							}
							str_append(&str, "\n");
						}
					}
					break;
				case IM_RGBA:
					{
						str_append(&str, "%d bit RGBA:\n", imagedepth);
						unsigned short *ptr=(unsigned short*)image->data;
						int iy=MAXVAR(iy1, 0);
						for(int yend=MINVAR(iy2+2, image->ih);iy<yend;++iy)
						{
							int ky=image2screen_y_int(iy);
							int ix=MAXVAR(ix1, 0);
							for(int xend=MINVAR(ix2+2, image->iw);ix<xend;++ix)
							{
								int kx=image2screen_x_int(ix);
								int idx=(image->iw*iy+ix)<<2;
								str_append(&str, "    ");
								str_append(&str, format, ptr[idx  ]>>(16-imagedepth));
								str_append(&str, format, ptr[idx+1]>>(16-imagedepth));
								str_append(&str, format, ptr[idx+2]>>(16-imagedepth));
								str_append(&str, format, ptr[idx+3]>>(16-imagedepth));
							}
							str_append(&str, "\n");
						}
					}
					break;
				case IM_BAYER:
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
			ImageHandle im2=paste_bmp_from_clipboard();
			if(im2)
			{
				image_free(&image);
				image_free(&impreview);
				image=image_construct(im2->iw, im2->ih, 16, im2->data, im2->iw, im2->ih, im2->xcap-im2->iw, im2->depth);
				impreview=im2;
				imagetype=IM_RGBA;
				if(imagecentered)
					center_image();
				return 1;
			}
		}
		break;
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
	case 'X'://toggle horizontal profile plot
		if(profileplotmode!=PROFILE_X)
			profileplotmode=PROFILE_X;
		else
			profileplotmode=PROFILE_OFF;
		return 1;
	case 'Y'://toggle vertical profile plot
		if(profileplotmode!=PROFILE_Y)
			profileplotmode=PROFILE_Y;
		else
			profileplotmode=PROFILE_OFF;
		return 1;
	case KEY_QUOTE://toggle bitplane view
		bitmode+=1-(GET_KEY_STATE(KEY_SHIFT)<<2);
		MODVAR(bitmode, bitmode, 3);
		if(bitmode&&bitplane==-1)
			bitplane=imagedepth-1;
		if(bitmode==1)
			MODVAR(bitplane, bitplane, imagedepth);
		update_image(0, 1);
		break;
	case KEY_LBRACKET://prev bitplane
		if(bitmode)
		{
			int n=bitmode==2?imagedepth*3:imagedepth;
			++bitplane;
			MODVAR(bitplane, bitplane, n);
			update_image(0, 1);
		}
		break;
	case KEY_RBRACKET://next bitplane
		if(bitmode)
		{
			int n=bitmode==2?imagedepth*3:imagedepth;
			--bitplane;
			MODVAR(bitplane, bitplane, n);
			update_image(0, 1);
		}
		break;
	case KEY_SPACE:
		if(image)
		{
			if(imagetype==IM_BAYER)
				test48(image, imagedepth, bayer);
			else
				test49(image, imagedepth);
		}
		break;
	}
	return 0;
}
int io_keyup(IOKey key, char c)
{
	switch(key)
	{
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
static void print_pixellabels(int ix1, int ix2, int iy1, int iy2, int xoffset, int yoffset, int component, char label, long long txtcolors, int is_bayer)
{
	unsigned short *ptr=(unsigned short*)image->data+((image->iw*yoffset+xoffset)<<2);
	const char *format;
	if(pxlabels_hex)
	{
		if(imagedepth<=4)
			format="%c %01X";
		else if(imagedepth<=8)
			format="%c %02X";
		else if(imagedepth<=12)
			format="%c %03X";
		else
			format="%c%04X";
	}
	else
		format="%c%5d";
	txtcolors=set_text_colors(txtcolors);
	int iy=MAXVAR(iy1, 0);
	iy>>=is_bayer;
	iy<<=is_bayer;
	float fontsize=1, labeloffset=is_bayer?0:tdy*fontsize*component;
	for(int yend=MINVAR(iy2+2, image->ih);iy<yend;iy+=1<<is_bayer)
	{
		int ky=image2screen_y_int(iy+yoffset);
		int ix=MAXVAR(ix1, 0);
		ix>>=is_bayer;
		ix<<=is_bayer;
		for(int xend=MINVAR(ix2+2, image->iw);ix<xend;ix+=1<<is_bayer)
		{
			int kx=image2screen_x_int(ix+xoffset);
			int idx=(image->iw*iy+ix)<<2;
			GUIPrint_enqueue(&vertices_text, 0, (float)kx, (float)ky+labeloffset, fontsize, format, label, ptr[idx+component]>>(16-imagedepth));
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
	if((unsigned)iy<(unsigned)impreview->ih)
	{
		unsigned char *row=0;
		int lgstride=0;
		switch(imagetype)
		{
		case IM_GRAYSCALE:	row=impreview->data+((size_t)impreview->iw*iy<<2|0	), lgstride=2;break;
		case IM_RGBA:		row=impreview->data+((size_t)impreview->iw*iy<<2|comp	), lgstride=2;break;
		case IM_BAYER:
		case IM_BAYER_SEPARATE:
			iy>>=1;
			iy<<=1;
			iy|=comp>>1;
			row=impreview->data+((impreview->iw*iy+(comp&1))<<2|bayer[comp]);
			lgstride=2;
			break;
		default:
			return;
		}
		int ix;
		float y2;
		float gain=(h>>1)/255.f;
		for(int kx=0;kx<w;++kx)
		{
			ix=screen2image_x_int(kx);
			if(imagetype==IM_BAYER||imagetype==IM_BAYER_SEPARATE)
			{
				ix>>=1;
				ix<<=1;
			}
			if((unsigned)ix<(unsigned)impreview->iw)
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
	if((unsigned)ix<(unsigned)impreview->iw)
	{
		unsigned char *col=0;
		int stride=0;
		switch(imagetype)
		{
		case IM_GRAYSCALE:	col=impreview->data+((size_t)ix<<2|0	), stride=impreview->iw<<2;break;
		case IM_RGBA:		col=impreview->data+((size_t)ix<<2|comp	), stride=impreview->iw<<2;break;
		case IM_BAYER:
		case IM_BAYER_SEPARATE:
			ix>>=1;
			ix<<=1;
			ix|=comp&1;
			col=impreview->data+((ix+(impreview->iw&-(comp>>1)))<<2|bayer[comp]);
			stride=impreview->iw<<2;
			break;
		default:
			return;
		}
		
		float x2;
		int iy;
		float gain=(w>>1)/255.f;
		for(int ky=0;ky<h;++ky)
		{
			iy=screen2image_y_int(ky);
			if(imagetype==IM_BAYER||imagetype==IM_BAYER_SEPARATE)
			{
				iy>>=1;
				iy<<=1;
			}
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
			if(imagetype==IM_GRAYSCALE)
				print_pixellabels(ix1, ix2, iy1, iy2, 0, 0, 0, 'g', theme[3], 0);
			else if(imagetype==IM_RGBA)
			{
				print_pixellabels(ix1, ix2, iy1, iy2, 0, 0, 0, 'r', theme[0], 0);
				print_pixellabels(ix1, ix2, iy1, iy2, 0, 0, 1, 'g', theme[1], 0);
				print_pixellabels(ix1, ix2, iy1, iy2, 0, 0, 2, 'b', theme[2], 0);
				if(zoom>=ZOOM_LIMIT_ALPHA)
					print_pixellabels(ix1, ix2, iy1, iy2, 0, 0, 3, 'a', theme[3], 0);
			}
			else if(imagetype==IM_BAYER)
			{
				print_pixellabels(ix1, ix2, iy1, iy2, 0, 0, bayer[0], labels[bayer[0]], theme[bayer[0]], 1);
				print_pixellabels(ix1, ix2, iy1, iy2, 1, 0, bayer[1], labels[bayer[1]], theme[bayer[1]], 1);
				print_pixellabels(ix1, ix2, iy1, iy2, 0, 1, bayer[2], labels[bayer[2]], theme[bayer[2]], 1);
				print_pixellabels(ix1, ix2, iy1, iy2, 1, 1, bayer[3], labels[bayer[3]], theme[bayer[3]], 1);
			}
		}
		if(hist_on)
		{
			switch(imagetype)
			{
			case IM_GRAYSCALE:
				draw_histogram(histogram, 256, 0x80808080, (int)tdy, (int)(h-tdy));
				break;
			case IM_RGBA:
			case IM_BAYER:
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
			void (*draw_profile)(int comp, int color)=profileplotmode==PROFILE_X?draw_profile_x:draw_profile_y;
			switch(imagetype)
			{
			case IM_GRAYSCALE:
				draw_profile(0, 0xFF000000);
				break;
			case IM_RGBA:
				draw_profile(0, 0xFF0000FF);
				draw_profile(1, 0xFF00FF00);
				draw_profile(2, 0xFFFF0000);
				draw_profile(3, 0xFF000000);
				break;
			case IM_BAYER:
				draw_profile(0, 0xFF000000|0xFF<<(bayer[0]<<3));
				draw_profile(1, 0xFF000000|0xFF<<(bayer[1]<<3));
				draw_profile(2, 0xFF000000|0xFF<<(bayer[2]<<3));
				draw_profile(3, 0xFF000000|0xFF<<(bayer[3]<<3));
				break;
			case IM_BAYER_SEPARATE:
				profileplotmode=PROFILE_OFF;//X
				break;
			}
		}
		
		const char *imtypestr="?";
		switch(imagetype)
		{
		case IM_UNINITIALIZED: imtypestr="IM_UNINITIALIZED"; break;
		case IM_GRAYSCALE:     imtypestr="IM_GRAYSCALE";     break;
		case IM_RGBA:          imtypestr="IM_RGBA";          break;
		case IM_BAYER:         imtypestr="IM_BAYER";         break;
		case IM_BAYER_SEPARATE:imtypestr="IM_BAYER_SEPARATE";break;
		}
		//g_printed=0;
		GUIPrint_append(0, 0, h-tdy, 1, 0, "XY(%5d, %5d) / WH %dx%d  x%lf  %s  depth %d  CR %10.6lf", imx, imy, impreview->iw, impreview->ih, zoom, imtypestr, imagedepth, format_CR);
		if(bitmode==1)
			GUIPrint_append(0, 0, h-tdy, 1, 0, "  Bitplane %d", bitplane);
		else if(bitmode==2)
			GUIPrint_append(0, 0, h-tdy, 1, 0, "  Ch %d Bitplane %d", bitplane/imagedepth, bitplane%imagedepth);
		if((unsigned)imx<(unsigned)impreview->iw&&(unsigned)imy<(unsigned)impreview->ih)
		{
			int idx=(impreview->iw*imy+imx)<<2;
			unsigned char *p=impreview->data+idx;
			unsigned short *p0=(unsigned short*)image->data+idx;
			switch(imagetype)
			{
			case IM_GRAYSCALE:
				if(has_alpha)
					GUIPrint_append(0, 0, h-tdy, 1, 0, "  GRAY_ALPHA(%3d, %3d)=0x%04X%04X", (unsigned)p[0], (unsigned)p[3], (unsigned)p0[0], (unsigned)p0[3]);
				else
					GUIPrint_append(0, 0, h-tdy, 1, 0, "  GRAY(%3d)=0x%04X", (unsigned)p[0], (unsigned)p0[0]);
				break;
			case IM_RGBA:
				{
					long long color;
					memcpy(&color, p0, sizeof(color));
					GUIPrint_append(0, 0, h-tdy, 1, 0, "  RGBA(%3d, %3d, %3d, %3d)=0x%016llX", (unsigned)p[0], (unsigned)p[1], (unsigned)p[2], (unsigned)p[3], color);
				}
				break;
			case IM_BAYER:
			case IM_BAYER_SEPARATE:
				{
					const char labels[]="RGB";
					int comp=(imy&1)<<1|imx&1;
					GUIPrint_append(0, 0, h-tdy, 1, 0, "  %c%c%c%c  %c(%5d)=0x%04X", labels[bayer[0]], labels[bayer[1]], labels[bayer[2]], labels[bayer[3]], labels[bayer[comp]], (unsigned)p0[bayer[comp]], (unsigned)p0[bayer[comp]]);
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
	extern int mouse_bypass;
	static double t=0;
	double t2=time_ms();
	GUIPrint(0, 0, 0, 1, "fps %lf", 1000./(t2-t));
	t=t2;
	swapbuffers();
}
int io_quit_request()//return 1 to exit
{
	return 1;
}
void io_cleanup()//cleanup
{
}