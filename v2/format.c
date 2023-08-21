#include"hView.h"
#include<libavformat/avformat.h>
#include<libavcodec/avcodec.h>
#include<libavutil/opt.h>
#include<libavutil/fifo.h>
#include<libavfilter/buffersrc.h>
#include<libavfilter/buffersink.h>
#include<libswscale/swscale.h>
#ifdef _MSC_VER
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "swscale.lib")
#endif
static const char file[]=__FILE__;


#define CHECKAV(E) (!(E)||LOG_ERROR("%s", av_err2str(E)))

//https://github.com/ShootingKing-AM/ffmpeg-pseudocode-tutorial
//https://github.com/leandromoreira/ffmpeg-libav-tutorial/blob/master/README.md
int load_media(const char *filename, ImageHandle *image)//TODO special loader for HEIC, AVIF
{
	int error;
	AVFormatContext *formatContext=avformat_alloc_context();
	if(!formatContext)
	{
		LOG_ERROR("Allocation error");
		return -1;
	}
	error=avformat_open_input(&formatContext, filename, 0, 0);
	CHECKAV(error);
	error=avformat_find_stream_info(formatContext, 0);	CHECKAV(error);

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
		LOG_WARNING("Cannot open \'%s\'", filename);
		return -1;
	}
	AVCodecContext *codecContext=avcodec_alloc_context3(codec);
	if(!codecContext)
	{
		LOG_ERROR("Allocation error");
		return -1;
	}
	error=avcodec_parameters_to_context(codecContext, codecParameters);	CHECKAV(error);
	error=avcodec_open2(codecContext, codec, NULL);	CHECKAV(error);
	AVFrame *frame=av_frame_alloc();
	AVPacket *packet=av_packet_alloc();
	if(!frame||!packet)
	{
		LOG_ERROR("Allocation error");
		return -1;
	}
	while((error=av_read_frame(formatContext, packet))>=0)
	{
		if(packet->stream_index==video_stream_index)
		{
			//int result=decode_packet(packet, codecContext, frame);
			error=avcodec_send_packet(codecContext, packet);	CHECKAV(error);
			while(error>=0)
			{
				error=avcodec_receive_frame(codecContext, frame);
				if(error==AVERROR(EAGAIN)||error==AVERROR_EOF)
					break;
				CHECKAV(error);
			
				enum AVPixelFormat format=frame->format;
				if(!sws_isSupportedInput(format))
				{
					LOG_ERROR("Unsupported input pixel format %d", format);
					return -1;
				}
				if(!sws_isSupportedOutput(AV_PIX_FMT_RGB32))
				{
					LOG_ERROR("Unsupported output pixel format %d", format);
					return -1;
				}
				AVFrame *frame2=av_frame_alloc();
				if(!frame2)
				{
					LOG_ERROR("Allocation error");
					return -1;
				}
				frame2->width=frame->width;
				frame2->height=frame->height;
				frame2->format=AV_PIX_FMT_RGBA;
				av_frame_get_buffer(frame2, 32);

				struct SwsContext *swsctx=sws_getContext(frame->width, frame->height, frame->format, frame2->width, frame2->height, frame2->format, SWS_FAST_BILINEAR, 0, 0, 0);
				sws_scale(swsctx, frame->data, frame->linesize, 0, frame->height, frame2->data, frame2->linesize);

				*image=image_construct(0, 0, frame2->data[0], frame2->width, frame2->height);
				if(!*image)
				{
					LOG_ERROR("Allocation error");
					return -1;
				}

				//int res=image[0]->iw*image[0]->ih;
				//for(int k=0;k<res;++k)//swap red & blue			0xAARRGGBB -> 0xAABBGGRR
				//{
				//	unsigned char *p=image[0]->data+(k<<2), temp;
				//	SWAPVAR(p[0], p[2], temp);
				//}

				sws_freeContext(swsctx);
				break;//get one frame
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
	return 0;
}