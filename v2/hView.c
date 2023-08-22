#include"hView.h"
#include<math.h>
static const char file[]=__FILE__;

//active keys turn on timer
#define ACTIVE_KEY_LIST\
	AK('W') AK('A') AK('S') AK('D') AK('T') AK('G')\
	AK(KEY_ENTER) AK(KEY_BKSP)\
	AK(KEY_UP) AK(KEY_DOWN)
//	AK(KEY_LEFT) AK(KEY_RIGHT)
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
double mousewheel_zoom=2;//mouse wheel factor
double wpx=0, wpy=0;//window position (top-left corner) in image coordinates

ArrayHandle fn=0;
ImageHandle image=0, impreview=0;
ImageType imagetype=IM_UNINITIALIZED;
int imagedepth=0;
char bayer[4]={0};//shift ammounts for the 4 Bayer mosaic components, -1 for grayscale, example: RGGB is {0, 8, 8, 16}

void update_image(int settitle, int render)
{
	if(!image)
		return;
	if(settitle)
		set_window_title("%s - hView", (char*)fn->data);
	if(!impreview||impreview->iw!=image->iw||impreview->ih!=image->ih)
	{
		if(impreview)
			image_free(&impreview);
		impreview=image_construct(0, 0, 8, 0, image->iw, image->ih, image->depth);
	}
	image_blit(impreview, 0, 0, image->data, image->iw, image->ih, image->depth);
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
	//const int standard=__STDC_VERSION__;
	if(argc>0)
	{
		fn=filter_path(argv[0], 1);
		load_media((char*)fn->data, &image);
		if(image)
			update_image(1, 0);
		else
			array_free(&fn);
	}
	if(!image)
		set_window_title("hView");
	return 1;
}
void center_image()
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
		return !timer;
	}
	return 1;
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
	switch(key)
	{
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

	case 'O':
		if(GET_KEY_STATE(KEY_CTRL))
		{
			ArrayHandle fn2=dialog_open_file(0, 0, 0);
			if(fn2)
			{
				ImageHandle im2=0;
				load_media(fn2->data, &im2);
				if(im2)
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
		
	case 'R':
		wpx=0, wpy=0, zoom=1;
		imagecentered=0;
		return 1;
	case 'C':
		center_image();
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
					if(!load_media(fn2[0]->data, &im2))
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
					return 1;
				}
				array_free(&filenames);
			}
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
void io_render()
{
	if(h<=0)
		return;
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
		const char *imtypestr="?";
		switch(imagetype)
		{
		case IM_UNINITIALIZED: imtypestr="IM_UNINITIALIZED"; break;
		case IM_GRAYSCALE:     imtypestr="IM_GRAYSCALE";     break;
		case IM_RGBA:          imtypestr="IM_RGBA";          break;
		case IM_BAYER:         imtypestr="IM_BAYER";         break;
		case IM_BAYER_SEPARATE:imtypestr="IM_BAYER_SEPARATE";break;
		}
		if((unsigned)imx<(unsigned)impreview->iw&&(unsigned)imy<(unsigned)impreview->ih)
		{
			unsigned char *p=impreview->data+((impreview->iw*imy+imx)<<2);
			int color;
			memcpy(&color, p, sizeof(color));
			GUIPrint(0, 0, h-tdy, 1, "M(%d, %d) / %dx%d  x%lf  %s  RGBA(%3d, %3d, %3d, %3d)=0x%08X", imx, imy, impreview->iw, impreview->ih, zoom, imtypestr, p[0], p[1], p[2], p[3], color);
		}
		else
			GUIPrint(0, 0, h-tdy, 1, "M(%d, %d) / %dx%d  x%lf  %s", imx, imy, impreview->iw, impreview->ih, zoom, imtypestr);
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