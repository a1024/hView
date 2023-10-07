#include"hView.h"
#include<libavformat/avformat.h>
#include<libavcodec/avcodec.h>
#include<libavutil/opt.h>
#include<libavutil/fifo.h>
#include<libavutil/imgutils.h>
#include<libavfilter/buffersrc.h>
#include<libavfilter/buffersink.h>
#include<libswscale/swscale.h>
#ifdef _MSC_VER
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "swscale.lib")
#endif
#ifdef HVIEW_INCLUDE_LIBHEIF
#include<libheif/heif.h>
#pragma comment(lib, "libheif-1.lib")
#pragma comment(lib, "liblibde265.lib")
#define CHECK_LIBHEIF(E)\
	do\
	{\
		if((E).code)\
		{\
			if(erroronfail)\
				LOG_WARNING("%s", (E).message);\
			return -1;\
		}\
	}while(0)
//#define CHECK_LIBHEIF(E) (!(E).code||LOG_WARNING("%s", (E).message))
#endif
#ifdef HVIEW_INCLUDE_LIBRAW
#include<libraw/libraw.h>
#pragma comment(lib, "libraw.lib")
#define CHECK_LIBRAW(E)\
	do\
	{\
		if(E)\
		{\
			if(erroronfail)\
				LOG_WARNING("Libraw error %d: %s", E, libraw_strerror(E));\
			return -1;\
		}\
	}while(0)
//#define CHECK_LIBRAW(E) ((E)==LIBRAW_SUCCESS||LOG_WARNING("Libraw error %d: %s", E, libraw_strerror(E)))
#endif
static const char file[]=__FILE__;

#define CHECK_AV(E)\
	do\
	{\
		if(E)\
		{\
			if(erroronfail)\
				LOG_WARNING("%s", av_err2str(E));\
			return -1;\
		}\
	}while(0)
//#define CHECK_AV(E) (!(E)||LOG_WARNING("%s", av_err2str(E)))

static void update_globals(const char *fn, ImageHandle image)//accesses globals
{
	filesize=get_filesize(fn);
	if(filesize>0)
	{
		int nch=0;
		unsigned short *data=(unsigned short*)image->data;

		if(imagedepth<16)
		{
			int half=1<<(16-imagedepth), mask=~(half-1);
			half>>=1;
			for(int k=0, res=image->iw*image->ih*4;k<res;++k)//round the pixels according to bitdepth
			{
				int val=data[k];
				val+=half;
				if(val>0xFFFF)
					val=0xFFFF;
				else
					val&=mask;
				data[k]=(unsigned short)val;
			}
		}
		
		if(has_alpha)
		{
			has_alpha=0;
			for(int k=0, res=image->iw*image->ih;k<res;++k)//check if alpha has information
			{
				if(data[k<<2|3]!=0xFFFF)
				{
					has_alpha=1;
					break;
				}
			}
		}
		switch(imagetype)
		{
		case IM_GRAYSCALE:
			nch=1;
			break;
		case IM_RGBA:
			{
				imagetype=IM_GRAYSCALE;
				for(int k=0, res=image->iw*image->ih;k<res;++k)//check for grayscale
				{
					if(data[k<<2]!=data[k<<2|1]||data[k<<2]!=data[k<<2|2])
					{
						imagetype=IM_RGBA;
						break;
					}
				}
				nch=imagetype==IM_GRAYSCALE?1:3;
			}
			break;
		case IM_BAYER:
			nch=1;
			break;
		}
		format_CR=(double)image->iw*image->ih*imagedepth*(nch+has_alpha)/(filesize*8);
	}
}

#ifdef HVIEW_INCLUDE_LIBHEIF
int load_heic(const char *filename, ImageHandle *image, int erroronfail)
{
	struct heif_context *ctx=heif_context_alloc();
#ifdef BENCHMARK
	long long t1=__rdtsc();
#endif
	struct heif_error error=heif_context_read_from_file(ctx, g_buf, 0);	CHECK_LIBHEIF(error);//TODO: file may not exist
	//if(error.code)
	//{
	//	if(erroronfail)
	//		LOG_WARNING("%s", error.message);
	//	return -1;
	//}

	struct heif_image_handle *handle=0;
	error=heif_context_get_primary_image_handle(ctx, &handle);			CHECK_LIBHEIF(error);//get a handle to the primary image

	heif_context_free(ctx);

	int iw2=heif_image_handle_get_width(handle), ih2=heif_image_handle_get_height(handle);

	struct heif_image *img=0;
	error=heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, 0);	CHECK_LIBHEIF(error);
	if(!img)
	{
		if(erroronfail)
			LOG_WARNING("LibHEIF decode error");
		return -1;
	}

	int stride=4;
	const uint8_t *data=heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);	CHECK_LIBHEIF(error);
#ifdef BENCHMARK
	long long t2=__rdtsc();
	LOG_WARNING("HEIC: %lld cycles", t2-t1);
#endif
	*image=image_construct(0, 0, 16, data, iw2, ih2, 0, 8);
	//assign_from_RGBA8((int*)data, iw2, ih2);

	has_alpha=heif_image_handle_has_alpha_channel(handle);
	imagedepth=heif_image_handle_get_luma_bits_per_pixel(handle);
	imagetype=IM_RGBA;

	heif_image_release(img);
	heif_image_handle_release(handle);
	
	update_globals(filename, *image);
	return 0;
}
#endif
#ifdef HVIEW_INCLUDE_LIBRAW
int load_raw(const char *filename, ImageHandle *image, int erroronfail)
{
	libraw_data_t *decoder=libraw_init(0);
	if(!decoder)
	{
		LOG_WARNING("Failed to initialize libraw decoder");
		return -1;
	}
	//int error=libraw_open_wfile(decoder, filename);
	int error=libraw_open_file(decoder, filename);
	if(error)
	{
		if(erroronfail)
			LOG_WARNING("Libraw error %d: %s", error, libraw_strerror(error));
		return -1;
	}
	
	int iw2=decoder->sizes.raw_width;
	int ih2=decoder->sizes.raw_height;
	error=libraw_unpack(decoder);	CHECK_LIBRAW(error);

	imagedepth=ceil_log2(decoder->color.maximum);
	imagetype=IM_BAYER;
	char color_sh[]={2, 1, 0, 1};//RGBG
	bayer[0]=color_sh[libraw_COLOR(decoder, 0, 0)];
	bayer[1]=color_sh[libraw_COLOR(decoder, 0, 1)];
	bayer[2]=color_sh[libraw_COLOR(decoder, 1, 0)];
	bayer[3]=color_sh[libraw_COLOR(decoder, 1, 1)];
	unsigned short *src=decoder->rawdata.raw_image;
	int res=iw2*ih2;
	switch(decoder->sizes.flip)
	{
	case 0:
		{
			*image=image_construct(0, 0, 16, 0, iw2, ih2, 0, 16);
			if(!*image)
			{
				LOG_WARNING("realloc returned null");
				return 0;
			}
			unsigned long long *dst=(unsigned long long*)image[0]->data;
			for(int ky=0;ky<ih2;++ky)
			{
				for(int kx=0;kx<iw2;++kx)
				{
					int sh=(bayer[(ky&1)<<1|kx&1]<<4)+16-imagedepth;
					*dst=0xFFFF000000000000|(unsigned long long)*src<<sh;
					++dst;
					++src;
				}
			}
		}
		break;
	case 3://upside-down
		{
			*image=image_construct(0, 0, 16, 0, iw2, ih2, 0, 16);
			if(!*image)
			{
				LOG_WARNING("realloc returned null");
				return 0;
			}
			unsigned long long *dst=(unsigned long long*)image[0]->data;
			char temp;
			SWAPVAR(bayer[0], bayer[2], temp);
			SWAPVAR(bayer[1], bayer[3], temp);
			for(int ky=0;ky<ih2;++ky)
			{
				for(int kx=0;kx<iw2;++kx)
				{
					int sh=(bayer[(ky&1)<<1|kx&1]<<4)+16-imagedepth;
					*dst=0xFFFF000000000000|(unsigned long long)src[iw2*(ih2-1-ky)+kx]<<sh;
					++dst;
				}
			}
		}
		break;
	case 5://90 degrees CCW
		{
			int temp=bayer[0];
			bayer[0]=bayer[1];
			bayer[1]=bayer[3];
			bayer[3]=bayer[2];
			bayer[2]=temp;
			SWAPVAR(iw2, ih2, temp);
			*image=image_construct(0, 0, 16, 0, iw2, ih2, 0, 16);
			if(!*image)
			{
				LOG_WARNING("realloc returned null");
				return 0;
			}
			unsigned long long *dst=(unsigned long long*)image[0]->data;
			for(int ky=0;ky<ih2;++ky)
			{
				for(int kx=0;kx<iw2;++kx)
				{
					int sh=(bayer[(ky&1)<<1|kx&1]<<4)+16-imagedepth;
					*dst=0xFFFF000000000000|(unsigned long long)src[ih2*kx+ih2-1-ky]<<sh;
					++dst;
				}
			}
		}
		break;
	case 6://90 degrees CW
		{
			int temp=bayer[0];
			bayer[0]=bayer[2];
			bayer[2]=bayer[3];
			bayer[3]=bayer[1];
			bayer[1]=temp;
			SWAPVAR(iw2, ih2, temp);
			*image=image_construct(0, 0, 16, 0, iw2, ih2, 0, 16);
			if(!*image)
			{
				LOG_WARNING("realloc returned null");
				return 0;
			}
			unsigned long long *dst=(unsigned long long*)image[0]->data;
			for(int ky=0;ky<ih2;++ky)
			{
				for(int kx=0;kx<iw2;++kx)
				{
					int sh=(bayer[(ky&1)<<1|kx&1]<<4)+16-imagedepth;
					*dst=0xFFFF000000000000|(unsigned long long)src[ih2*(iw2-1-kx)+ky]<<sh;
					++dst;
				}
			}
		}
		break;
	default:
		LOG_WARNING("Invalid RAW image orientation");
		break;
	}
	libraw_free_image(decoder);
	libraw_close(decoder);
	
	has_alpha=0;//only edited RAWs (eg: Photoshop) can have alpha
	update_globals(filename, *image);
	return 0;
}
#endif


//https://github.com/ShootingKing-AM/ffmpeg-pseudocode-tutorial
//https://github.com/leandromoreira/ffmpeg-libav-tutorial/blob/master/README.md
int load_media(const char *filename, ImageHandle *image, int erroronfail)//TODO special loader for HEIC, AVIF, RAW
{
	int len=(int)strlen(filename);
#ifdef HVIEW_INCLUDE_LIBHEIF
	if(
		!_stricmp(filename+len-5, ".AVIF")||//libheif opens avif too (NEED LIBAVIF)
		!_stricmp(filename+len-5, ".HEIC")
	)
		return load_heic(filename, image, erroronfail);
#endif
#ifdef HVIEW_INCLUDE_LIBRAW
	if(
		!_stricmp(filename+len-4, ".CR2")||
		!_stricmp(filename+len-4, ".CRW")||
		!_stricmp(filename+len-4, ".NEF")||
		!_stricmp(filename+len-4, ".REF")||
		!_stricmp(filename+len-4, ".DNG")||
		!_stricmp(filename+len-4, ".MOS")||
		!_stricmp(filename+len-4, ".KDC")||
		!_stricmp(filename+len-4, ".DCR")
	)
		return load_raw(filename, image, erroronfail);
#endif

	int error;
	AVFormatContext *formatContext=avformat_alloc_context();
	if(!formatContext)
	{
		LOG_WARNING("Allocation error");
		return -1;
	}
	error=avformat_open_input(&formatContext, filename, 0, 0);
	CHECK_AV(error);
	error=avformat_find_stream_info(formatContext, 0);	CHECK_AV(error);

	AVCodec const *codec=0;
	AVCodecParameters *codecParameters=0;
	int video_stream_index=-1;
	for(unsigned i=0;i<formatContext->nb_streams;++i)
	{
		AVStream *stream=formatContext->streams[i];
		AVCodec const *localCodec=avcodec_find_decoder(stream->codecpar->codec_id);
		if(!localCodec)
		{
			unsigned version=avcodec_version();
			LOG_WARNING("This codec is not supported on this build of libavcodec %d.%d.%d", version>>16&0xFF, version>>8&0xFF, version&0xFF);
			continue;
		}
		if(stream->codecpar->codec_type==AVMEDIA_TYPE_VIDEO)
		{
			if(video_stream_index==-1)
			{
				video_stream_index=i;
				codec=localCodec;
				codecParameters=stream->codecpar;
			}
		}
	}
	if(video_stream_index==-1)//FIXME
	{
		if(erroronfail)
			LOG_WARNING("Cannot open \'%s\'", filename);
		return -1;
	}
	AVCodecContext *codecContext=avcodec_alloc_context3(codec);
	if(!codecContext)
	{
		if(erroronfail)
			LOG_WARNING("Allocation error");
		return -1;
	}
	error=avcodec_parameters_to_context(codecContext, codecParameters);	CHECK_AV(error);
	error=avcodec_open2(codecContext, codec, NULL);	CHECK_AV(error);
	AVFrame *frame=av_frame_alloc();
	AVPacket *packet=av_packet_alloc();
	if(!frame||!packet)
	{
		LOG_WARNING("Allocation error");
		return -1;
	}
	while((error=av_read_frame(formatContext, packet))>=0)
	{
		if(packet->stream_index==video_stream_index)
		{
			//int result=decode_packet(packet, codecContext, frame);
			error=avcodec_send_packet(codecContext, packet);	CHECK_AV(error);
			while(error>=0)
			{
				error=avcodec_receive_frame(codecContext, frame);
				if(error==AVERROR(EAGAIN)||error==AVERROR_EOF)
					break;
				CHECK_AV(error);
			
				enum AVPixelFormat format=frame->format;
				if(!sws_isSupportedInput(format))
				{
					if(erroronfail)
						LOG_WARNING("Unsupported input pixel format %d", format);
					return -1;
				}
				if(!sws_isSupportedOutput(AV_PIX_FMT_RGB32))
				{
					if(erroronfail)
						LOG_WARNING("Unsupported output pixel format %d", format);
					return -1;
				}
				AVFrame *frame2=av_frame_alloc();
				if(!frame2)
				{
					LOG_WARNING("Allocation error");
					return -1;
				}
				frame2->width=frame->width;
				frame2->height=frame->height;
				frame2->format=AV_PIX_FMT_RGBA64LE;
				av_frame_get_buffer(frame2, 32);

				struct SwsContext *swsctx=sws_getContext(frame->width, frame->height, frame->format, frame2->width, frame2->height, frame2->format, 0, 0, 0, 0);
				sws_scale(swsctx, frame->data, frame->linesize, 0, frame->height, frame2->data, frame2->linesize);

				int padding=abs(frame2->linesize[0])/8-frame2->width;//division by 8 is because a single pixel is 64-bit (8 bytes)
				if(padding<0)
					padding=0;
				*image=image_construct(0, 0, 16, frame2->data[0], frame2->width, frame2->height, padding, 16);
				if(!*image)
				{
					LOG_WARNING("Allocation error");
					return -1;
				}
				
				AVPixFmtDescriptor const *desc=av_pix_fmt_desc_get(frame->format);
				//int bpp0=av_get_bits_per_pixel(desc);
				//int bpp=av_get_bits_per_sample(codec->id);//returns 0

				has_alpha=desc->nb_components==4||desc->nb_components==2;
				imagedepth=desc->comp->depth;
				//imagedepth=bpp0/desc->nb_components;//X
				imagetype=IM_RGBA;

				//int res=image[0]->iw*image[0]->ih;
				//for(int k=0;k<res;++k)//swap red & blue			0xAARRGGBB -> 0xAABBGGRR
				//{
				//	unsigned char *p=image[0]->data+(k<<2), temp;
				//	SWAPVAR(p[0], p[2], temp);
				//}

				sws_freeContext(swsctx);
				av_frame_free(&frame2);
				break;//get just the first frame
			}
		}
		av_packet_unref(packet);
		if(*image)
			break;//get one frame
	}
	avformat_close_input(&formatContext);
	av_packet_free(&packet);
	av_frame_free(&frame);
	avcodec_free_context(&codecContext);
	
	update_globals(filename, *image);
	return 0;
}