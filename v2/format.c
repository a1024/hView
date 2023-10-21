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
		if(E<0)\
		{\
			if(erroronfail)\
				LOG_WARNING("%s", av_err2str(E));\
			return -1;\
		}\
	}while(0)
//#define CHECK_AV(E) (!(E)||LOG_WARNING("%s", av_err2str(E)))

int   slic2_save(const char *filename, int iw, int ih, int nch, int depth, const void *src);
void* slic2_load(const char *filename, int *ret_iw, int *ret_ih, int *ret_nch, int *ret_depth, int *ret_dummy_alpha, int force_alpha);


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

		long long sum[3]={0}, count=0;//set background as far as possible from averate border color in RGB space
		for(int kx=0;kx<image->iw;++kx)//accumulate top edge
		{
			sum[0]+=data[kx<<2|0];
			sum[1]+=data[kx<<2|1];
			sum[2]+=data[kx<<2|2];
			++count;
		}
		for(int kx=0;kx<image->iw;++kx)//accumulate bottom edge
		{
			sum[0]+=data[(image->iw*(image->ih-1)+kx)<<2|0];
			sum[1]+=data[(image->iw*(image->ih-1)+kx)<<2|1];
			sum[2]+=data[(image->iw*(image->ih-1)+kx)<<2|2];
			++count;
		}
		for(int ky=0;ky<image->ih;++ky)//accumulate left edge
		{
			sum[0]+=data[image->iw*ky<<2|0];
			sum[1]+=data[image->iw*ky<<2|1];
			sum[2]+=data[image->iw*ky<<2|2];
			++count;
		}
		for(int ky=0;ky<image->ih;++ky)//accumulate right edge
		{
			sum[0]+=data[(image->iw*ky+image->iw-1)<<2|0];
			sum[1]+=data[(image->iw*ky+image->iw-1)<<2|1];
			sum[2]+=data[(image->iw*ky+image->iw-1)<<2|2];
			++count;
		}
		memset(background, 0, sizeof(background));
		int comp;
		comp=(int)(sum[0]/count>>8), background[0]=(unsigned char)(CLAMP(0, comp, 255)+128);
		comp=(int)(sum[1]/count>>8), background[1]=(unsigned char)(CLAMP(0, comp, 255)+128);
		comp=(int)(sum[2]/count>>8), background[2]=(unsigned char)(CLAMP(0, comp, 255)+128);
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
	int ret=0;
	if(!src)
	{
		if(erroronfail)
			LOG_WARNING("Cannot open \'%s\'", filename);
		ret=-1;
		goto libraw_fail;
	}
	switch(decoder->sizes.flip)
	{
	case 0:
		{
			*image=image_construct(0, 0, 16, 0, iw2, ih2, 0, 16);
			if(!*image)
			{
				LOG_WARNING("realloc returned null");
				return -1;
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
				return -1;
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
				return -1;
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
				return -1;
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
	has_alpha=0;//only edited RAWs (eg: Photoshop) can have alpha
	update_globals(filename, *image);

libraw_fail:
	libraw_free_image(decoder);
	libraw_close(decoder);
	return ret;
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

	if(
		!_stricmp(filename+len-4, ".SLI")
	)
	{
		int iw=0, ih=0, nch=0, dummy_alpha=0;
		void *ret=slic2_load(filename, &iw, &ih, &nch, &imagedepth, &dummy_alpha, 1);
		if(!ret)
			return -1;
		has_alpha=(nch==4||nch==2)&&!dummy_alpha;
		imagetype=IM_RGBA;
		*image=image_construct(iw, ih, 16, ret, iw, ih, 0, imagedepth<=8?8:16);
		free(ret);
		update_globals(filename, *image);
		return 0;
	}

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


int save_media(const char *fn, ImageHandle image, int erroronfail)
{
	enum AVPixelFormat srcfmt=image->depth==16?AV_PIX_FMT_RGBA64LE:AV_PIX_FMT_RGBA;
	int error=0;
	AVFormatContext *oc=0;
	error=avformat_alloc_output_context2(&oc, 0, 0, fn);	CHECK_AV(error);
	if(!oc&&erroronfail)
	{
		LOG_WARNING("Allocation error");
		return -1;
	}
	AVStream *stream=avformat_new_stream(oc, 0);
	if(!stream)
	{
		LOG_WARNING("Allocation error");
		return -1;
	}
	
	int len=(int)strlen(fn);//get short name
	int k=len-1;
	for(;k>=0&&fn[k]!='.';--k);
	k+=k!=0;//skip '.'
	k=len-k;
	char ext[MAX_PATH]={0};
	for(int k2=0;k2<k+1;++k2)
		ext[k2]=tolower(fn[len-k+k2]);

	enum AVCodecID codecid=AV_CODEC_ID_NONE;
		 if(!strcmp(ext, "png"))    codecid=AV_CODEC_ID_PNG;
	else if(!strcmp(ext, "jxl"))    codecid=AV_CODEC_ID_JPEGXL;
	else if(!strcmp(ext, "webp"))   codecid=AV_CODEC_ID_WEBP;
	else if(!strcmp(ext, "jpg"))    codecid=AV_CODEC_ID_MJPEG;
	else if(!strcmp(ext, "gif"))    codecid=AV_CODEC_ID_GIF;
	else if(!strcmp(ext, "jp2"))    codecid=AV_CODEC_ID_JPEG2000;
	else if(!strcmp(ext, "bmp"))    codecid=AV_CODEC_ID_BMP;
	else if(!strcmp(ext, "tif"))    codecid=AV_CODEC_ID_TIFF;
	else if(!strcmp(ext, "qoi"))    codecid=AV_CODEC_ID_QOI;
	else if(!strcmp(ext, "ljpg"))   codecid=AV_CODEC_ID_LJPEG;
	else if(!strcmp(ext, "jls"))    codecid=AV_CODEC_ID_JPEGLS;
	else if(!strcmp(ext, "loco"))   codecid=AV_CODEC_ID_LOCO;
	else if(!strcmp(ext, "ppm"))    codecid=AV_CODEC_ID_PPM;
	else if(!strcmp(ext, "pbm"))    codecid=AV_CODEC_ID_PBM;
	else if(!strcmp(ext, "pgm"))    codecid=AV_CODEC_ID_PGM;
	else if(!strcmp(ext, "pam"))    codecid=AV_CODEC_ID_PAM;
	if(codecid==AV_CODEC_ID_NONE)
	{
		LOG_WARNING("Cannot save as \'%s\'", fn);
		return -1;
	}

	//AVOutputFormat const *dstfmt=av_guess_format(ext, 0, 0);//gives NULL, should be video_codec AV_CODEC_ID_PNG
	//if(!dstfmt)
	//{
	//	LOG_WARNING("Cannot save \'%s\'", fn);
	//	return -1;
	//}
	AVCodec const *codec=avcodec_find_encoder(codecid);
	if(!codec)
	{
		LOG_WARNING("Cannot save \'%s\'", fn);
		return -1;
	}

	AVCodecContext *cc=avcodec_alloc_context3(codec);
	if(!cc)
	{
		LOG_WARNING("Allocation error");
		return -1;
	}
	
	//snprintf(g_buf, G_BUF_SIZE, "w=%d", image->iw);
	//error=av_set_options_string(cc, g_buf, "=", ":");	CHECK_AV(error);//Option not found
	//snprintf(g_buf, G_BUF_SIZE, "h=%d", image->ih);
	//error=av_set_options_string(cc, g_buf, "=", ":");	CHECK_AV(error);

	//snprintf(g_buf, G_BUF_SIZE, "%d", image->iw);
	//error=av_opt_set(cc, "width", g_buf, 0);		CHECK_AV(error);
	//snprintf(g_buf, G_BUF_SIZE, "%d", image->ih);
	//error=av_opt_set(cc, "height", g_buf, 0);	CHECK_AV(error);

#if 1
	//avcodec.h(line 426)
	cc->width=image->iw;
	cc->height=image->ih;
	cc->pix_fmt=srcfmt;
	cc->time_base.num=1;
	cc->time_base.den=1;
	cc->gop_size=0;
	cc->max_b_frames=0;
	//cc->bit_rate=?;
#endif

	//TODO set codec options (which depend on the codec)
	AVDictionary *opt=0;
	av_dict_set(&opt, "slow", 0, 0);

	error=avcodec_open2(cc, codec, &opt);	CHECK_AV(error);//-22: Invalid Argument
	AVFrame *frame=av_frame_alloc();
	frame->width=image->iw;
	frame->height=image->ih;
	frame->format=image->depth==16?AV_PIX_FMT_RGBA64LE:AV_PIX_FMT_RGBA;
	error=av_frame_get_buffer(frame, 0);		CHECK_AV(error);
	av_image_fill_arrays(frame->data, frame->linesize, image->data, srcfmt, image->iw, image->ih, 1);

	AVPacket packet={0};
	//av_init_packet(&packet);//deprecated warning
	//packet.data=0;
	//packet.size=0;

	//save file
	FILE *f=fopen(fn, "wb");
	if(!f)
		LOG_WARNING("Cannot save \'%s\'", fn);
	else
	{
		error=avcodec_send_frame(cc, frame);	CHECK_AV(error);
		while(error>=0)
		{
			error=avcodec_receive_packet(cc, &packet);
			if(error==AVERROR(EAGAIN)||error==AVERROR_EOF)
				break;
			CHECK_AV(error);

			fwrite(packet.data, 1, packet.size, f);
			av_packet_unref(&packet);
		}

		//flush encoder
		error=avcodec_send_frame(cc, 0);		CHECK_AV(error);
		while(error>=0)
		{
			error=avcodec_receive_packet(cc, &packet);
			if(error==AVERROR(EAGAIN)||error==AVERROR_EOF)
				break;
			CHECK_AV(error);

			fwrite(packet.data, 1, packet.size, f);
			av_packet_unref(&packet);
		}
		fclose(f);
	}

	av_frame_free(&frame);
	avcodec_free_context(&cc);
	avformat_free_context(oc);
	return 0;
}
int save_media_as(ImageHandle image, const char *initialname, int namelen, int erroronfail)
{
	Filter filters[]=
	{
		{"\'Png is Not Gnu, which in turn is not Unix\' File (*.PNG)", ".PNG"},
		{"JPEG XL File (*.JXL)", ".JXL"},
		{"Simple Lossless Image Codec (*.SLI)", ".SLI"},
		//{"WebP File (*.WEBP)", ".WEBP"},//error
		//{"JPEG File (*.JPG)", ".JPG"},//error
		//{"GIF File (*.GIF)", ".GIF"},//error
		//{"JPEG2000 File (*.JP2)", ".JP2"},//error
		//{"BMP File (*.BMP)", ".BMP"},//error
		{"TIFF File (*.TIF)", ".TIF"},
		{"Quite OK Image (*.QOI)", ".QOI"},
		//{"Lossless JPEG (*.LJPG)", ".LJPG"},//error
		//{"JPEG-LS (*.JLS)", ".JLS"},//error
		//{"LOCO File (*.LOCO)", ".LOCO"},//error
		//{"PPM File (*.PPM)", ".PPM"},//error
		//{"PBM File (*.PBM)", ".PBM"},//error
		//{"PGM File (*.PGM)", ".PGM"},//error
		//{"PAM File (*.PAM)", ".PAM"},//error
	};
	ArrayHandle name;
	STR_COPY(name, initialname, namelen);
	STR_APPEND(name, ".PNG", 4, 1);
	int ext_selection=0, ret;
	char *fn=dialog_save_file(filters, _countof(filters), name->data, &ext_selection);
	array_free(&name);
	if(!fn)
		ret=-2;
	else
	{
		if(ext_selection==3)//.SLI
		{
			ret=slic2_save(fn, image->iw, image->ih, 4, image->depth, image->data);
			if(!ret)
			{
				LOG_WARNING("Failed to save \'%s\'", fn);
				ret=-1;
			}
		}
		else
			ret=save_media(fn, image, erroronfail);
		free(fn);
	}
	return ret;
}