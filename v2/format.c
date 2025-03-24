#include"hView.h"
#include<stdlib.h>
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
#ifdef _MSC_VER
#pragma comment(lib, "libheif-1.lib")
#pragma comment(lib, "liblibde265.lib")
#endif
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
#ifdef _MSC_VER
#pragma comment(lib, "libraw.lib")
#endif
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


static void update_globals(const char *fn, Image16 *image)//accesses globals
{
	filesize=get_filesize(fn);
	if(filesize>0)
	{
		int nch=0;
		const unsigned short *data=image->data;

		//if(imagedepth<16)
		//{
		//	int half=1<<(16-imagedepth), mask=~(half-1);
		//	half>>=1;
		//	for(int k=0, res=image->iw*image->ih*4;k<res;++k)//round the pixels according to bitdepth
		//	{
		//		int val=data[k];
		//		val+=half;
		//		if(val>0xFFFF)
		//			val=0xFFFF;
		//		else
		//			val&=mask;
		//		data[k]=(unsigned short)val;
		//	}
		//}
		
		switch(imagetype)
		{
		case IM_GRAYSCALEv2:
			has_alpha=0;//FIXME G+A
			nch=1;
			break;
		case IM_RGBA:
			{
#if 0
				if(has_alpha)
				{
					has_alpha=0;
					for(ptrdiff_t k=0, res=(ptrdiff_t)image->iw*image->ih;k<res;++k)//check if alpha has information
					{
					//	if(data[k<<2|3]!=0xFFFF)
						if(data[k<<2|3]!=data[3])
						{
							has_alpha=1;
							break;
						}
					}
				}
				imagetype=IM_GRAYSCALEv2;
				for(ptrdiff_t k=0, res=(ptrdiff_t)image->iw*image->ih;k<res;++k)//check for grayscale
				{
					if(data[k<<2|0]!=data[k<<2|1]||data[k<<2|0]!=data[k<<2|2])
					{
						imagetype=IM_RGBA;
						break;
					}
				}
				nch=imagetype==IM_GRAYSCALEv2?1:3;
#endif
			}
			break;
		case IM_BAYERv2:
			has_alpha=0;
			nch=1;
			break;
		}
		format_CR=(double)image->iw*image->ih*imagedepth*(nch+has_alpha)/(filesize*8);

		if(imagetype==IM_BAYERv2||imagetype==IM_GRAYSCALEv2)
			memset(background, 128, sizeof(background));
		else
		{
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
			background[0]=(unsigned char)((sum[0]/count<<8>>image->depth)+128);
			background[1]=(unsigned char)((sum[1]/count<<8>>image->depth)+128);
			background[2]=(unsigned char)((sum[2]/count<<8>>image->depth)+128);
			background[3]=0;
			//background[3]=255;
		}
	}
}



#ifdef HVIEW_INCLUDE_LIBHEIF
static int load_heic(const char *filename, Image16 **image, int erroronfail)
{
	struct heif_context *ctx=heif_context_alloc();
#ifdef BENCHMARK
	long long t1=__rdtsc();
#endif
	struct heif_error error=heif_context_read_from_file(ctx, g_buf, 0);	CHECK_LIBHEIF(error);//TODO: file may not exist
	if(error.code)
	{
		if(erroronfail)
			LOG_WARNING("%s", error.message);
		heif_context_free(ctx);
		return -1;
	}

	struct heif_image_handle *handle=0;
	error=heif_context_get_primary_image_handle(ctx, &handle);	CHECK_LIBHEIF(error);//get a handle to the primary image

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
	has_alpha=heif_image_handle_has_alpha_channel(handle);
	imagedepth=heif_image_handle_get_luma_bits_per_pixel(handle);
	imagetype=IM_RGBA;
#ifdef BENCHMARK
	long long t2=__rdtsc();
	LOG_WARNING("HEIC: %lld cycles", t2-t1);
#endif
	*image=image_alloc16(0, iw2, ih2, 4, imagedepth);
	unsigned short *dstptr=image[0]->data;
	for(ptrdiff_t k=0, res=(ptrdiff_t)iw2*ih2;k<res;++k)
		dstptr[k]=data[k];
	//*image=image_construct(0, 0, 16, data, iw2, ih2, 0, 8);
	//assign_from_RGBA8((int*)data, iw2, ih2);


	heif_image_release(img);
	heif_image_handle_release(handle);
	
	update_globals(filename, *image);
	return 0;
}
#endif
#ifdef HVIEW_INCLUDE_LIBRAW
static int load_raw(const char *filename, Image16 **image, int erroronfail)
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
	
	//int iw2=decoder->sizes.raw_width;
	//int ih2=decoder->sizes.raw_height;
	error=libraw_unpack(decoder);		CHECK_LIBRAW(error);
	error=libraw_raw2image(decoder);	CHECK_LIBRAW(error);
	const int rgbidx[]={0, 1, 2, 1};//assuming  decoder->rawdata.iparams.cdesc == "RGBG"
	bayer[0]=rgbidx[decoder->rawdata.iparams.filters>>0*2&3];
	bayer[1]=rgbidx[decoder->rawdata.iparams.filters>>1*2&3];
	bayer[2]=rgbidx[decoder->rawdata.iparams.filters>>2*2&3];
	bayer[3]=rgbidx[decoder->rawdata.iparams.filters>>3*2&3];
	int iw2=decoder->sizes.raw_width;
	int ih2=decoder->sizes.raw_height;
	//switch(decoder->sizes.flip)
	//{
	//case 0:break;//upright
	//case 3:break;//upside-down
	//case 5://90 degrees CCW
	//case 6://90 degrees CW
	//	{
	//		int temp;
	//		SWAPVAR(iw2, ih2, temp);
	//	}
	//	break;
	//}
	//unsigned short (*src)[4]=decoder->image;
	imagedepth=ceil_log2(decoder->color.maximum);
	if(*(int*)bayer)
	{
#if 0
		int iw3=(iw2+1)&~1, ih3=(ih2+1)&~1;
		imagetype=IM_GRAYSCALEv2;
		*image=image_alloc16(0, iw3, ih3, 1, imagedepth);
		if(!*image)
		{
			LOG_WARNING("Alloc error");
			return 0;
		}
		memset(image[0]->data, 0, sizeof(short)*iw3*ih3);
		for(int ky=0;ky<ih2;++ky)//can't use memcpy because of odd->even dimension padding
		{
			const unsigned short *srcptr=(const unsigned short*)decoder->rawdata.raw_alloc+iw2*ky;
			unsigned short *dstptr=image[0]->data+iw3*ky;
			for(int kx=0;kx<iw2;++kx)
				*dstptr++=*srcptr++;
		}
#endif
#if 1
		int iw3=(iw2+1)&~1, ih3=(ih2+1)&~1;
		imagetype=IM_BAYERv2;
		*image=image_alloc16(0, iw3, ih3, 1, imagedepth);
		if(!*image)
		{
			LOG_WARNING("Alloc error");
			return 0;
		}
		memset(image[0]->data, 0, sizeof(short)*iw3*ih3);
		for(int ky=0;ky<ih2;++ky)//can't use memcpy because of odd->even dimension padding
		{
			const unsigned short *srcptr=(const unsigned short*)decoder->rawdata.raw_alloc+iw2*ky;
			//const unsigned short *srcptr=
			//	(const unsigned short*)decoder->rawdata.raw_alloc
			//	+decoder->sizes.raw_width*(decoder->sizes.raw_inset_crops->ctop+ky)
			//	+decoder->sizes.raw_inset_crops->cleft;
			unsigned short *dstptr=image[0]->data+iw3*ky;
			for(int kx=0;kx<iw2;++kx)
				*dstptr++=*srcptr++;
		}
		//memcpy(image[0]->data, decoder->rawdata.raw_alloc, sizeof(short)*iw2*ih2);
#endif
	}
	else
	{
		imagetype=IM_RGBA;
		*image=image_alloc16(0, iw2, ih2, 4, imagedepth);
		if(!*image)
		{
			LOG_WARNING("Alloc error");
			return 0;
		}
		memcpy(image[0]->data, decoder->rawdata.raw_alloc, sizeof(short)*iw2*ih2);
		for(ptrdiff_t k=0, res=(ptrdiff_t)4*iw2*ih2;k<res;k+=4)//set alpha to 1
			image[0]->data[k+3]=(1<<imagedepth)-1;
	}
#if 1
	switch(decoder->sizes.flip)
	{
	case 0:break;//upright
	case 3://requires 180
		image_inplacexflip(*image);
		image_inplaceyflip(*image);
		{
			int temp;
			SWAPVAR(bayer[0], bayer[3], temp);
			SWAPVAR(bayer[1], bayer[2], temp);
		}
		break;
	case 5://requires 90 CCW	actually CW
		image_inplacexflip(*image);
		image_transpose(image);
		{
			int temp=bayer[0];
			bayer[0]=bayer[1];
			bayer[1]=bayer[3];
			bayer[3]=bayer[2];
			bayer[2]=temp;
		}
		break;
	case 6://requires 90 CW		actually CCW
		image_transpose(image);
		image_inplacexflip(*image);
		{
			int temp=bayer[0];
			bayer[0]=bayer[2];
			bayer[2]=bayer[3];
			bayer[3]=bayer[1];
			bayer[1]=temp;
		}
		break;
	}
#endif
	libraw_free_image(decoder);
	libraw_close(decoder);
	
	has_alpha=0;
	update_globals(filename, *image);
	return 0;
}
#endif


//.huf
typedef struct _HuffHeader//24 bytes
{
	char HUFF[4];//'H'|'U'<<8|'F'<<16|'F'<<24
	unsigned version;//1: huffman, {2,3,4}: encoded with palette, 5: RVL, 7: ACC-ANS, 10: uncompressed raw10 packing, 12: uncompressed raw12 packing
	unsigned width, height;//uncompressed dimensions
	char bayerInfo[4];//'G'|'R'<<8|'B'<<16|'G'<<24 for Galaxy A70
	unsigned nLevels;//1<<bitDepth, also histogram size for version==1
	unsigned histogram[];//compressed data begins at histogram+nLevels
} HuffHeader;
typedef struct _HuffDataHeader//16 bytes
{
	char DATA[4];//'D'|'A'<<8|'T'<<16|'A'<<24
	unsigned uPxCount;//uncompressed pixel count
	unsigned long long cBitSize;//compressed data size in bits
	unsigned data[];
} HuffDataHeader;
typedef struct _HuffNode
{
	int branch[2];
	unsigned short value;
	int freq;
} HuffNode;
typedef struct _HuffDecodeCell
{
	unsigned short syms[8], nsyms, bitsconsumed;
	int depth;
	struct _HuffDecodeCell **next;
} HuffDecodeCell;
static void huf_tree_debugprint(HuffNode *tree, int tsize, int rootidx)
{
	console_start();
	for(int k=rootidx;k>=0;--k)
	{
		HuffNode *node=tree+k;
		if(!node->freq)
			continue;
		console_log("[%d] 0:%d,1:%d, freq=%d, val=%d\n", k, node->branch[0], node->branch[1], node->freq, node->value);
	}
	//console_pause();
}
static void huf_tree_debugcheck_r(HuffNode *tree, int tsize, int idx, unsigned char *visited)
{
	if(idx!=-1)
	{
		HuffNode *node=tree+idx;
		if((unsigned)idx>=(unsigned)tsize||visited[idx])
			LOG_ERROR("");
		visited[idx]=1;
		huf_tree_debugcheck_r(tree, tsize, node->branch[0], visited);
		huf_tree_debugcheck_r(tree, tsize, node->branch[1], visited);
	}
}
static void huf_tree_debugcheck(HuffNode *tree, int tsize, int rootidx)
{
	unsigned char *visited=(unsigned char*)malloc(tsize);
	if(!visited)
	{
		LOG_ERROR("Alloc error");
		return;
	}
	memset(visited, 0, tsize);
	huf_tree_debugcheck_r(tree, tsize, rootidx, visited);
	free(visited);
}
#define HUFF_GT(LIDX, RIDX) ((tree[LIDX].freq==tree[RIDX].freq&&(LIDX)<(RIDX))||tree[LIDX].freq>tree[RIDX].freq)
static void huf_insert(int *pqueue, HuffNode *tree, int *pqsize, int insertedidx)
{
	int qsize=*pqsize;
	pqueue[qsize]=insertedidx;
	++qsize;
	*pqsize=qsize;
	for(int idx=qsize-1;idx>0;)
	{
		int parent=(idx-1)>>1;
		if(HUFF_GT(pqueue[parent], pqueue[idx]))
		{
			int temp;
			SWAPVAR(pqueue[parent], pqueue[idx], temp);
		}
		else
			break;
		idx=parent;
	}
}
static int huf_extractmin(int *pqueue, HuffNode *tree, int *pqsize)
{
	int minidx=*pqueue;
	int qsize=--*pqsize;
	*pqueue=pqueue[qsize];
	for(int idx=0;idx<qsize;)
	{
		int leftidx2=idx*2+1, rightidx2=leftidx2+1, largestidx;
		if(leftidx2<qsize&&HUFF_GT(pqueue[idx], pqueue[leftidx2]))
			largestidx=leftidx2;
		else
			largestidx=idx;
		if(leftidx2<qsize&&HUFF_GT(pqueue[largestidx], pqueue[rightidx2]))
			largestidx=rightidx2;
		if(largestidx==idx)
			break;
		SWAPVAR(pqueue[idx], pqueue[largestidx], leftidx2);
		idx=largestidx;
	}
	return minidx;
}
static void huf_buildtable(HuffDecodeCell *table, HuffNode const *tree, int tsize, int nhistbits, int root)
{
	table->next=(HuffDecodeCell**)malloc(sizeof(size_t[256]));
	if(!table->next)
	{
		LOG_ERROR("Alloc error");
		return;
	}
	for(int ks=0;ks<256;++ks)
	{
		HuffDecodeCell *noden=(HuffDecodeCell*)malloc(sizeof(HuffDecodeCell));
		if(!noden)
		{
			LOG_ERROR("Alloc error");
			return;
		}
		memset(noden, 0, sizeof(*noden));

		int idx=root;
		noden->depth=nhistbits;
		noden->bitsconsumed=0xFFFF;
		for(int kdepth=0;kdepth<7;++kdepth)
		{
			HuffNode const *node1=tree+idx;
			int bit=ks>>kdepth&1;//bits are read in reverse order (MSB<-LSB) for compatibility
			int idx2=node1->branch[bit];
			if(idx2==-1)
			{
				noden->syms[noden->nsyms++]=node1->value;
				noden->bitsconsumed=kdepth;
				idx=root;
				if(nhistbits)
					break;
			}
			else
				idx=idx2;
		}
		if(noden->bitsconsumed==0xFFFF)
		{
			if(nhistbits+8>1024)
				LOG_ERROR("Error");
			huf_buildtable(noden, tree, tsize, nhistbits+8, idx);
			noden->bitsconsumed=8;
		}
		table->next[ks]=noden;
	}
}
static void huf_freetable(HuffDecodeCell *table)
{
	if(table)
	{
		if(table->next)
		{
			for(int ks=0;ks<256;++ks)
				huf_freetable(table->next[ks]);
			free(table->next);
		}
		if(table->depth)
			free(table);
	}
}
static int huf_load(const char *filename, Image16 **image, int erroronfail)
{
	unsigned char *srcbuf=0;
	ptrdiff_t srcsize=0;
	{
		srcsize=get_filesize(filename);
		if(srcsize<1)
		{
			if(erroronfail)
				LOG_WARNING("Cannot open %s", filename);
			return -1;
		}
		FILE *fsrc=fopen(filename, "rb");
		if(!fsrc)
		{
			if(erroronfail)
				LOG_WARNING("Cannot open %s", filename);
			return -1;
		}
		srcbuf=(unsigned char*)malloc(srcsize+16);
		if(!srcbuf)
		{
			fclose(fsrc);
			LOG_ERROR("Alloc error");
			return -1;
		}
		fread(srcbuf, 1, srcsize, fsrc);
		fclose(fsrc);
	}
	HuffHeader *header=(HuffHeader*)srcbuf;
	if(*(int*)header->HUFF!=('H'|'U'<<8|'F'<<16|'F'<<24))
	{
		if(erroronfail)
			LOG_WARNING("Cannot open %s", filename);
		free(srcbuf);
		return -1;
	}
	unsigned char bayer_sh2[4]={0};
	if(*(int*)header->bayerInfo)
	{
		for(int k=0;k<4;++k)
		{
			switch(header->bayerInfo[k])
			{
			case 'R':bayer_sh2[k]=32;break;
			case 'G':bayer_sh2[k]=16;break;
			case 'B':bayer_sh2[k]= 0;break;
			default:
				LOG_WARNING("Invalid Bayer info: %08X = %c%c%c%c",
					*(int*)header->bayerInfo,
					header->bayerInfo[0],
					header->bayerInfo[1],
					header->bayerInfo[2],
					header->bayerInfo[3]
				);
				free(srcbuf);
				return -1;
			}
		}
	}
	else
		memset(bayer_sh2, -1, 4);
	int res=header->width*header->height;
	if(header->version==1)
	{
		HuffDataHeader *hData=(HuffDataHeader*)(header->histogram+header->nLevels);
		unsigned *bitstream=hData->data;
		HuffDecodeCell decroot={0};
		if(*(int*)hData->DATA!=('D'|'A'<<8|'T'<<16|'A'<<24))
		{
			if(erroronfail)
				LOG_WARNING("Cannot open %s", filename);
			free(srcbuf);
			return -1;
		}
		imagetype=IM_BAYERv2;
		imagedepth=ceil_log2(header->nLevels);
		bayer[0]=bayer_sh2[0]>>4;
		bayer[1]=bayer_sh2[1]>>4;
		bayer[2]=bayer_sh2[2]>>4;
		bayer[3]=bayer_sh2[3]>>4;
		bayer_sh2[0]+=16-imagedepth;
		bayer_sh2[1]+=16-imagedepth;
		bayer_sh2[2]+=16-imagedepth;
		bayer_sh2[3]+=16-imagedepth;
		{
			//build tree
			int *hist=(int*)header->histogram;
			int nlevels=header->nLevels;
			int treecap=nlevels*sizeof(HuffNode[2]);//2*nlevels-1 nodes
			HuffNode *tree=(HuffNode*)malloc(treecap);
			int pqueuecap=nlevels*(int)sizeof(int);
			int *pqueue=(int*)malloc(pqueuecap);//minheap-based priority queue
			if(!pqueue||!tree)
			{
				LOG_ERROR("Alloc error");
				return -1;
			}
			memset(tree, 0, treecap);
			memset(pqueue, 0, pqueuecap);
			int tsize=0;
			int qsize=0;
			for(int ks=0;ks<nlevels;++ks)
			{
				int freq=hist[ks];
				if(freq)
				{
					HuffNode *node=tree+tsize;
					node->branch[0]=-1;
					node->branch[1]=-1;
					node->value=ks;
					node->freq=freq;
					huf_insert(pqueue, tree, &qsize, tsize);
					++tsize;
				}
			}
			while(qsize>1)
			{
				int lidx=huf_extractmin(pqueue, tree, &qsize);
				int ridx=huf_extractmin(pqueue, tree, &qsize);
				HuffNode *node=tree+tsize;
				node->branch[0]=lidx;
				node->branch[1]=ridx;
				node->value=-1;
				node->freq=tree[lidx].freq+tree[ridx].freq;
				huf_insert(pqueue, tree, &qsize, tsize);
				++tsize;
			}
			int rootidx=*pqueue;
			free(pqueue);

			*image=image_alloc16(0, header->width, header->height, 1, imagedepth);
		//	*image=image_construct(0, 0, 16, 0, header->width, header->height, 0, 16);

			//debug decode
#if 1
			unsigned short *dstptr=(unsigned short*)image[0]->data;
			int bitidx=0, kd=0, kx=0, ky=0;
			unsigned state=*bitstream;
			while(kd<res&&bitidx<hData->cBitSize)
			{
				int prev=rootidx;//
				int node=rootidx;
				while(bitidx<hData->cBitSize&&(tree[node].branch[0]!=-1||tree[node].branch[1]!=-1))
				{
					int bit=bitstream[bitidx>>5]>>(bitidx&31)&1;
					prev=node;//
					node=tree[node].branch[bit];
					++bitidx;
				}
				dstptr[kd++]=tree[node].value;
			//	dstptr[kd++]=0xFFFF000000000000|(unsigned long long)tree[node].value<<bayer_sh2[(ky&1)<<1|kx&1];
				++kx;
				if((unsigned)kx>=header->width)
					++ky, kx=0;
			}
#endif

#if 0
		//	huf_tree_debugcheck(tree, tsize, rootidx);
		//	huf_tree_debugprint(tree, tsize, rootidx);

			//make 8-ary tree from binary tree
			huf_buildtable(&decroot, tree, tsize, 0, rootidx);
#endif
			free(tree);
		}
#if 0
		unsigned state=*bitstream;
		unsigned long long *dstptr=(unsigned long long*)image[0]->data;
		int srcbitidx=32;
		int dstidx=0, kx=0, ky=0;
		for(;dstidx<res;)
		{
			HuffDecodeCell *bytenode=decroot.next[state&0xFF];
			int nwritten=0;
			while(!bytenode->nsyms)
			{
				for(int k=0;k<8;++k)
				{
					if(srcbitidx>=hData->cBitSize)
					{
						if(erroronfail)
							LOG_WARNING("Decode error");
						huf_freetable(&decroot);
						free(srcbuf);
						return -1;
					}
					int bit=bitstream[srcbitidx>>5]>>(srcbitidx&31)&1;
					state=bit<<31|state>>1;
					++srcbitidx;
				}
				HuffDecodeCell *nextnode=bytenode->next[state&0xFF];
				if(!nextnode)
				{
					if(erroronfail)
						LOG_WARNING("Decode error");
					huf_freetable(&decroot);
					free(srcbuf);
					return -1;
				}
				bytenode=nextnode;
			}
			switch(bytenode->nsyms)
			{
			case 8:dstptr[dstidx++]=0xFFFF000000000000|(unsigned long long)bytenode->syms[nwritten++]<<bayer_sh2[(ky&1)<<1|kx&1]; ++kx; if(kx>=header->width)++ky, kx=0;
			case 7:dstptr[dstidx++]=0xFFFF000000000000|(unsigned long long)bytenode->syms[nwritten++]<<bayer_sh2[(ky&1)<<1|kx&1]; ++kx; if(kx>=header->width)++ky, kx=0;
			case 6:dstptr[dstidx++]=0xFFFF000000000000|(unsigned long long)bytenode->syms[nwritten++]<<bayer_sh2[(ky&1)<<1|kx&1]; ++kx; if(kx>=header->width)++ky, kx=0;
			case 5:dstptr[dstidx++]=0xFFFF000000000000|(unsigned long long)bytenode->syms[nwritten++]<<bayer_sh2[(ky&1)<<1|kx&1]; ++kx; if(kx>=header->width)++ky, kx=0;
			case 4:dstptr[dstidx++]=0xFFFF000000000000|(unsigned long long)bytenode->syms[nwritten++]<<bayer_sh2[(ky&1)<<1|kx&1]; ++kx; if(kx>=header->width)++ky, kx=0;
			case 3:dstptr[dstidx++]=0xFFFF000000000000|(unsigned long long)bytenode->syms[nwritten++]<<bayer_sh2[(ky&1)<<1|kx&1]; ++kx; if(kx>=header->width)++ky, kx=0;
			case 2:dstptr[dstidx++]=0xFFFF000000000000|(unsigned long long)bytenode->syms[nwritten++]<<bayer_sh2[(ky&1)<<1|kx&1]; ++kx; if(kx>=header->width)++ky, kx=0;
			case 1:dstptr[dstidx++]=0xFFFF000000000000|(unsigned long long)bytenode->syms[nwritten++]<<bayer_sh2[(ky&1)<<1|kx&1]; ++kx; if(kx>=header->width)++ky, kx=0;
			}
			for(int k=0;k<bytenode->bitsconsumed;++k)
			{
				if(srcbitidx>=hData->cBitSize)
				{
					if(erroronfail)
						LOG_WARNING("Decode error");
					huf_freetable(&decroot);
					free(srcbuf);
					return -1;
				}
				int bit=bitstream[srcbitidx>>5]>>(srcbitidx&31)&1;
				state=bit<<31|state>>1;
				++srcbitidx;
			}
		}
		huf_freetable(&decroot);
#endif
	}
	else if(header->version==5)//RVL
	{
		imagetype=IM_BAYERv2;
		imagedepth=ceil_log2(header->nLevels);
		bayer[0]=bayer_sh2[0]>>4;
		bayer[1]=bayer_sh2[1]>>4;
		bayer[2]=bayer_sh2[2]>>4;
		bayer[3]=bayer_sh2[3]>>4;
		bayer_sh2[0]+=16-imagedepth;
		bayer_sh2[1]+=16-imagedepth;
		bayer_sh2[2]+=16-imagedepth;
		bayer_sh2[3]+=16-imagedepth;
		int *bitstream=(int*)header->histogram;
		int bitidx=0;
		int interleave=*(int*)header->bayerInfo!=0&&*(int*)header->bayerInfo!=1;
		int w2=header->width>>1, h2=header->height>>1;
		*image=image_alloc16(0, header->width, header->height, 1, imagedepth);
	//	*image=image_construct(0, 0, 16, 0, header->width, header->height, 0, 16);
		unsigned short *dstptr=(unsigned short*)image[0]->data;
		for(int ky=0;ky<(int)header->height;++ky)
		{
			int prev=0;
			
			for(int kx=0;kx<(int)header->width;++kx)
			{
				int symbol=0;
				int code, bitlen=0;
				do
				{
					code=bitstream[bitidx>>5]>>(bitidx&31)&15;
					symbol|=(code&7)<<bitlen;
					bitlen+=3, bitidx+=4;
				}while(code&8);

				symbol=symbol>>1^-(symbol&1);
				prev+=symbol;

				*dstptr++=prev;
			}
#if 0
			int iy2=ky>=h2;
			int ky0=ky-(h2&-iy2);	//ky0 = ky>=h/2 ? (ky-h/2)*2+1 : ky*2	= {0, 2, 4, ...h&~1, 1, 3, 5, ...h}
			ky0=ky0<<1|iy2;
			if(!interleave)ky0=ky;

			unsigned short *drow=(unsigned short*)image[0]->data+header->width*ky0;
			for(int kx=0;kx<(int)header->width;++kx)
			{
				int ix2=kx>=w2;
				int kx0=kx-(w2&-ix2);
				kx0=kx0<<1|ix2;
				if(!interleave)kx0=kx;

				int symbol=0;
				int code, bitlen=0;
				do
				{
					code=bitstream[bitidx>>5]>>(bitidx&31)&15;
					symbol|=(code&7)<<bitlen;
					bitlen+=3, bitidx+=4;
				}while(code&8);

				symbol=symbol>>1^-(symbol&1);
				prev+=symbol;

				drow[kx0]=0xFFFF000000000000|(unsigned long long)prev<<bayer_sh2[(ky0&1)<<1|kx0&1];
			}
#endif
		}
	}
	else
	{
		if(erroronfail)
			LOG_WARNING("FIXME");
		free(srcbuf);
		return -1;
	}
	free(srcbuf);
	update_globals(filename, *image);
	return 0;
}


//GR
static int gr_save(const char *dstfn, short *image, int iw, int ih, int nlevels, char *bayermatrix)
{
	int psize=(iw+16)*(int)sizeof(short[2*2]);//2 padded rows * 1 channel * {pixels, errors}
	short *pixels=(short*)malloc(psize);
	int dstcap=iw*ih<<1;
	unsigned long long *dstbuf=(unsigned long long*)malloc(dstcap), *dstptr=dstbuf;
	if(!pixels||!dstbuf)
	{
		LOG_ERROR("Alloc error");
		return 1;
	}
	memset(pixels, 0, psize);

	const int half=0;
//	int half=nlevels>>1;

	unsigned long long cache=0;
	int nbits=(int)sizeof(cache)<<3;
	for(int ky=0, idx=0;ky<ih;++ky)
	{
		int W=0, eW=0;
		short *rows[]=
		{
			pixels+((iw+16LL)*((ky+0LL)&1)+8)*2,
			pixels+((iw+16LL)*((ky-1LL)&1)+8)*2,
		};
		for(int kx=0;kx<iw;++kx, ++idx)
		{
			int
				NW	=rows[1][0-1*2],
				N	=rows[1][0+0*2],
			//	W	=rows[0][0-1*2],
				eNEEE	=rows[1][1+3*2];
			//	eW	=rows[0][1-1*2];
			int vmax=N, vmin=W, pred, nbypass, sym;
			if(N<W)vmin=N, vmax=W;
			pred=N+W-NW;
			CLAMP2(pred, vmin, vmax);

		//	nbypass=FLOOR_LOG2(eW+(eW<4));
			nbypass=FLOOR_LOG2(eW+1);

			sym=image[idx]-half;
			rows[0][0]=W=sym;
			sym-=pred;
			sym=sym<<1^sym>>31;
			rows[0][1]=eW=(2*eW+sym+eNEEE)>>2;

			//gr_enc
			{
				int nzeros=sym>>nbypass, bypass=sym&((1<<nbypass)-1);
				if(nzeros>=nbits)//fill the rest of cache with zeros, and flush
				{
					nzeros-=nbits;
					*dstptr++=cache;
					if(nzeros>=(int)(sizeof(cache)<<3))//just flush zeros
					{
						cache=0;
						do
						{
							nzeros-=(sizeof(cache)<<3);
							*dstptr++=cache;
						}
						while(nzeros>(int)(sizeof(cache)<<3));
					}
					cache=0;
					nbits=(sizeof(cache)<<3);
				}
				//now there is room for zeros:  0 <= nzeros < nbits <= 64
				nbits-=nzeros;//emit remaining zeros to cache

				bypass|=1<<nbypass;//append 1 stop bit
				++nbypass;
				if(nbypass>=nbits)//not enough free bits in cache:  fill cache, write to list, and repeat
				{
					nbypass-=nbits;
					cache|=(unsigned long long)bypass>>nbypass;
					bypass&=(1<<nbypass)-1;
					*dstptr++=cache;
					cache=0;
					nbits=sizeof(cache)<<3;
				}
				//now there is room for bypass:  0 <= nbypass < nbits <= 64
				nbits-=nbypass;//emit remaining bypass to cache
				cache|=(unsigned long long)bypass<<nbits;
			}

			rows[0]+=2;
			rows[1]+=2;
		}
	}
	*dstptr++=cache;
	free(pixels);
	{
		int streamsize=(int)(dstptr-dstbuf);
		FILE *fdst=fopen(dstfn, "wb");
		if(!fdst)
		{
			LOG_ERROR("Cannot open \"%s\" for writing", dstfn);
			return 1;
		}
		fprintf(fdst, "GR %d %d %d %c%c%c%c\n",
			iw, ih, nlevels,
			bayermatrix[0],
			bayermatrix[1],
			bayermatrix[2],
			bayermatrix[3]
		);
		fwrite(dstbuf, 1, streamsize*sizeof(long long), fdst);
		fclose(fdst);
	}
	free(dstbuf);
	return 0;
}
static short* gr_load(const char *srcfn, int *ret_iw, int *ret_ih, int *ret_nlevels, char *ret_bayermatrix)
{
	unsigned char *srcbuf=0;
	ptrdiff_t srcsize=get_filesize(srcfn);
	if(srcsize<1)
	{
		LOG_ERROR("Cannot open \"%s\"", srcfn);
		return 0;
	}
	{
		FILE *fsrc=fopen(srcfn, "rb");
		if(!fsrc)
		{
			LOG_ERROR("Cannot open \"%s\"", srcfn);
			return 0;
		}
		srcbuf=(unsigned char*)malloc(srcsize+16);
		if(!srcbuf)
		{
			LOG_ERROR("Alloc error");
			return 0;
		}
		fread(srcbuf, 1, srcsize, fsrc);
		fclose(fsrc);
	}
	unsigned char *srcptr=srcbuf;
	int iw=0, ih=0, nlevels=0;
	{
		if(memcmp(srcptr, "GR ", 3))
		{
			LOG_ERROR("Invalid file");
			free(srcbuf);
			return 0;
		}
		srcptr+=3;
		while((unsigned)(*srcptr-'0')<10)
			iw=10*iw+*srcptr++-'0';
		if(*srcptr++!=' ')
		{
			LOG_ERROR("Invalid file");
			free(srcbuf);
			return 0;
		}
		while((unsigned)(*srcptr-'0')<10)
			ih=10*ih+*srcptr++-'0';
		if(*srcptr++!=' ')
		{
			LOG_ERROR("Invalid file");
			free(srcbuf);
			return 0;
		}
		while((unsigned)(*srcptr-'0')<10)
			nlevels=10*nlevels+*srcptr++-'0';
		if(*srcptr++!=' ')
		{
			LOG_ERROR("Invalid file");
			free(srcbuf);
			return 0;
		}
		if(ret_bayermatrix)
			memcpy(ret_bayermatrix, srcptr, 4);
		srcptr+=4;
		if(*srcptr++!='\n')
		{
			LOG_ERROR("Invalid file");
			free(srcbuf);
			return 0;
		}
	}
	int psize=(iw+16)*(int)sizeof(short[2*2]);//2 padded rows * 1 channel * {pixels, errors}
	short *pixels=(short*)malloc(psize);
	int imsize=iw*ih*(int)sizeof(short);
	short *image=(short*)malloc(imsize);
	if(!pixels||!image)
	{
		LOG_ERROR("Alloc error");
		return 0;
	}
	memset(pixels, 0, psize);
	
	const int half=0;
//	int half=nlevels>>1;

	unsigned long long *streamptr=(unsigned long long*)srcptr, cache=0;
	int nbits=0;
	for(int ky=0, idx=0;ky<ih;++ky)
	{
		int W=0, eW=0;
		short *rows[]=
		{
			pixels+((iw+16LL)*((ky+0LL)&1)+8)*2,
			pixels+((iw+16LL)*((ky-1LL)&1)+8)*2,
		};
		for(int kx=0;kx<iw;++kx, ++idx)
		{
			int
				NW	=rows[1][0-1*2],
				N	=rows[1][0+0*2],
			//	W	=rows[0][0-1*2],
				eNEEE	=rows[1][1+3*2];
			//	eW	=rows[0][1-1*2];
			int vmax=N, vmin=W, pred, nbypass, sym;
			if(N<W)vmin=N, vmax=W;
			pred=N+W-NW;
			CLAMP2(pred, vmin, vmax);

		//	nbypass=FLOOR_LOG2(eW+(eW<4));
			nbypass=FLOOR_LOG2(eW+1);

			//gr_dec
			{
				//cache: MSB 00[hhh]ijj LSB		nbits 6->3, h is about to be read (past codes must be cleared from cache)
				
				int nleadingzeros=0;
				sym=0;
				if(!nbits)//cache is empty
					goto read;
				for(;;)//cache reading loop
				{
					nleadingzeros=(int)_lzcnt_u64(cache);
					nleadingzeros+=nbits-64;
				//	nleadingzeros=nbits-FLOOR_LOG2_P1(cache);//count leading zeros

					//if(nleadingzeros<0)
					//	LOG_ERROR("");
					nbits-=nleadingzeros;//remove accessed zeros
					sym+=nleadingzeros;

					if(nbits)
						break;
				read://cache is empty
					cache=*streamptr++;
					nbits=sizeof(cache)<<3;
				}
				//now  0 < nbits <= 64
				--nbits;
				//now  0 <= nbits < 64
				cache-=1ULL<<nbits;//remove stop bit

				unsigned bypass=0;
				sym<<=nbypass;
				if(nbits<nbypass)
				{
					nbypass-=nbits;
					bypass|=(int)(cache<<nbypass);
					cache=*streamptr++;
					nbits=sizeof(cache)<<3;
				}
				if(nbypass)
				{
					nbits-=nbypass;
					bypass|=(int)(cache>>nbits);
					cache&=(1ULL<<nbits)-1;
				}
				sym|=bypass;
			}
			
			rows[0][1]=eW=(2*eW+sym+eNEEE)>>2;

			sym=sym>>1^-(sym&1);
			sym+=pred;
			image[idx]=sym+half;

			//if((unsigned)image[idx]>=(unsigned)nlevels)
			//	LOG_ERROR("");

			rows[0][0]=W=sym;

			rows[0]+=2;
			rows[1]+=2;
		}
	}
	free(pixels);
	free(srcbuf);
	*ret_iw=iw;
	*ret_ih=ih;
	*ret_nlevels=nlevels;
	return image;
}


//https://github.com/ShootingKing-AM/ffmpeg-pseudocode-tutorial
//https://github.com/leandromoreira/ffmpeg-libav-tutorial/blob/master/README.md
int load_media(const char *filename, Image16 **image, int erroronfail)//TODO special loader for HEIC, AVIF, RAW
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
		!_stricmp(filename+len-4, ".CR3")||
		!_stricmp(filename+len-4, ".CRW")||
		!_stricmp(filename+len-4, ".DCR")||
		!_stricmp(filename+len-4, ".DNG")||
		!_stricmp(filename+len-4, ".KDC")||
		!_stricmp(filename+len-4, ".MOS")||
		!_stricmp(filename+len-4, ".NEF")||
		!_stricmp(filename+len-4, ".REF")
	)
		return load_raw(filename, image, erroronfail);
#endif

	if(
		!_stricmp(filename+len-3, ".GR")
	)
	{
		int iw=0, ih=0, nlevels=0;
		char bayermatrix[4]={0};
		short *buf=gr_load(filename, &iw, &ih, &nlevels, bayermatrix);

		iw<<=1;//it's a planar Bayer stack
		ih>>=1;

		for(int kc=0;kc<4;++kc)
			bayermatrix[kc]&=0xDF;
		int iw2=iw>>1, ih2=ih>>1;
		ptrdiff_t res4=(ptrdiff_t)iw2*ih2;
		short *comp[]=
		{
			(short*)buf+res4*0,
			(short*)buf+res4*1,
			(short*)buf+res4*2,
			(short*)buf+res4*3,
		};
		has_alpha=0;
		imagetype=IM_BAYERv2;
		imagedepth=FLOOR_LOG2(nlevels);
		imagedepth+=(1<<imagedepth)<nlevels;//CEIL_LOG2
		*image=image_alloc16(0, iw, ih, 1, imagedepth);
	//	*image=image_construct(iw, ih, 16, 0, iw, ih, 0, imagedepth<=8?8:16);
		bayer[0]=(bayermatrix[0]=='R'?0:(bayermatrix[0]=='G'?1:2));
		bayer[1]=(bayermatrix[1]=='R'?0:(bayermatrix[1]=='G'?1:2));
		bayer[2]=(bayermatrix[2]=='R'?0:(bayermatrix[2]=='G'?1:2));
		bayer[3]=(bayermatrix[3]=='R'?0:(bayermatrix[3]=='G'?1:2));
		unsigned short *dstptr=(unsigned short*)image[0]->data;
		for(int ky=0, idx=0;ky<ih;++ky)
		{
			for(int kx=0;kx<iw;++kx, ++idx)
			{
				int kc=(ky&1)<<1|(kx&1);
				dstptr[idx]=comp[kc][iw2*(ky>>1)+(kx>>1)];
			}
		}
#if 0
		char sh[]=
		{
			(bayer[0]<<4)+16-imagedepth,
			(bayer[1]<<4)+16-imagedepth,
			(bayer[2]<<4)+16-imagedepth,
			(bayer[3]<<4)+16-imagedepth,
		};
		unsigned long long *dstptr=(unsigned long long*)image[0]->data;
		for(int ky=0, idx=0;ky<ih;++ky)
		{
			for(int kx=0;kx<iw;++kx, ++idx)
			{
				//if(ky==2016&&kx==1588)//
				//	printf("");

				int kc=(ky&1)<<1|(kx&1);
				dstptr[idx]=0xFFFF000000000000|(unsigned long long)comp[kc][iw2*(ky>>1)+(kx>>1)]<<sh[kc];
			}
		}
#endif
		free(buf);
		update_globals(filename, *image);
		return 0;
	}

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
		*image=image_alloc16(0, iw, ih, nch>1?4:1, imagedepth);
		unsigned short *dstptr=image[0]->data;
		switch(nch)
		{
		case 1:
			if(imagedepth>8)
			{
				const unsigned short *srcptr=(const unsigned short*)ret;
				for(ptrdiff_t k=0, res=(ptrdiff_t)iw*ih;k<res;++k)
					dstptr[k]=srcptr[k];
			}
			else
			{
				const unsigned char *srcptr=(const unsigned char*)ret;
				for(ptrdiff_t k=0, res=(ptrdiff_t)iw*ih;k<res;++k)
					dstptr[k]=srcptr[k];
			}
			break;
		case 2:
			if(imagedepth>8)
			{
				const unsigned short *srcptr=(const unsigned short*)ret;
				for(ptrdiff_t k=0, res=(ptrdiff_t)iw*ih;k<res;++k)
					dstptr[k]=srcptr[k*2];
			}
			else
			{
				const unsigned char *srcptr=(const unsigned char*)ret;
				for(ptrdiff_t k=0, res=(ptrdiff_t)iw*ih;k<res;++k)
					dstptr[k]=srcptr[k*2];
			}
			break;
		case 3:
			LOG_WARNING("Unreachable");
			break;
		case 4:
			if(imagedepth>8)
			{
				const unsigned short *srcptr=(const unsigned short*)ret;
				for(ptrdiff_t k=0, res=(ptrdiff_t)4*iw*ih;k<res;++k)
					dstptr[k]=srcptr[k];
			}
			else
			{
				const unsigned char *srcptr=(const unsigned char*)ret;
				for(ptrdiff_t k=0, res=(ptrdiff_t)4*iw*ih;k<res;++k)
					dstptr[k]=srcptr[k];
			}
			break;
		}
	//	*image=image_construct(iw, ih, 16, ret, iw, ih, 0, imagedepth<=8?8:16);
		free(ret);
		update_globals(filename, *image);
		return 0;
	}

	if(
		!_stricmp(filename+len-4, ".HUF")
	)
		return huf_load(filename, image, erroronfail);

	if(
		!_stricmp(filename+len-4, ".DOC")||
		!_stricmp(filename+len-5, ".DOCX")||
		!_stricmp(filename+len-4, ".ODF")||
		!_stricmp(filename+len-4, ".ODS")||
		!_stricmp(filename+len-4, ".PPT")||
		!_stricmp(filename+len-5, ".PPTX")||
		!_stricmp(filename+len-4, ".XLS")||
		!_stricmp(filename+len-5, ".XLSX")||
		!_stricmp(filename+len-4, ".PDF")||
		!_stricmp(filename+len-4, ".TXT")||
		!_stricmp(filename+len-4, ".SVG")
	)
		return 1;

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
				
				AVPixFmtDescriptor const *desc=av_pix_fmt_desc_get(frame->format);
				//int bpp0=av_get_bits_per_pixel(desc);
				//int bpp=av_get_bits_per_sample(codec->id);//returns 0

				*image=image_alloc16(0, frame2->width, frame2->height, 4, desc->comp->depth);
			//	*image=image_alloc16((unsigned short*)frame2->data[0], frame2->width, frame2->height, 4, desc->comp->depth);
			//	*image=image_construct(0, 0, 16, frame2->data[0], frame2->width, frame2->height, padding, 16);
				if(!*image)
				{
					LOG_ERROR("Alloc error");
					return -1;
				}
				has_alpha=desc->nb_components==4||desc->nb_components==2;
				imagedepth=desc->comp->depth;
				//imagedepth=bpp0/desc->nb_components;//X
				imagetype=IM_RGBA;
				const unsigned short *srcptr=(const unsigned short*)frame2->data[0];
				unsigned short *dstptr=image[0]->data;
				int rowstride=image[0]->nch*(image[0]->iw+padding);
				for(int ky=0;ky<image[0]->ih;++ky)
				{
					const unsigned short *srcptr2=srcptr;
					for(int kx=0;kx<image[0]->iw;++kx)
					{
						for(int kc=0;kc<image[0]->nch;++kc)
							*dstptr++=*srcptr2++>>(16-imagedepth);
					}
					srcptr+=rowstride;
				}
				//for(ptrdiff_t k=0, res=(ptrdiff_t)image[0]->nch*image[0]->iw*image[0]->ih;k<res;++k)
				//{
				//	image[0]->data[k]=*srcptr++>>(16-imagedepth);
				//	//if((k&3)==3)
				//	//	image[0]->data[k]=255;
				//}

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


int save_media(const char *fn, Image8 *image, int erroronfail)
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
	     if(!strcmp(ext, "png"))	codecid=AV_CODEC_ID_PNG;
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
int save_media_as(Image8 *image, const char *initialname, int namelen, int erroronfail)
{
	Filter filters[]=
	{
		{"\'Png is Not Gnu, which in turn is not Unix\' File (*.PNG)", ".PNG"},
		{"JPEG XL File (*.JXL)", ".JXL"},
		{"Simple Lossless Image Codec (*.SLI)", ".SLI"},
	//	{"WebP File (*.WEBP)", ".WEBP"},//error
	//	{"JPEG File (*.JPG)", ".JPG"},//error
	//	{"GIF File (*.GIF)", ".GIF"},//error
	//	{"JPEG2000 File (*.JP2)", ".JP2"},//error
	//	{"BMP File (*.BMP)", ".BMP"},//error
		{"TIFF File (*.TIF)", ".TIF"},
		{"Quite OK Image (*.QOI)", ".QOI"},
	//	{"Lossless JPEG (*.LJPG)", ".LJPG"},//error
	//	{"JPEG-LS (*.JLS)", ".JLS"},//error
	//	{"LOCO File (*.LOCO)", ".LOCO"},//error
		{"PPM File (*.PPM)", ".PPM"},//error
	//	{"PBM File (*.PBM)", ".PBM"},//error
		{"PGM File (*.PGM)", ".PGM"},//error
	//	{"PAM File (*.PAM)", ".PAM"},//error
	};
	ArrayHandle name;
	STR_COPY(name, initialname, namelen);
	STR_APPEND(name, ".PNG", 4, 1);
	int ext_selection=0, ret=-1;
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
		else if(ext_selection==6)//PPM
		{
			size_t res=(size_t)image->iw*image->ih, usize=3*res;
			unsigned char *dstbuf=(unsigned char*)malloc(usize);
			if(!dstbuf)
			{
				LOG_ERROR("Alloc error");
				return -1;
			}
			for(int k=0, kd=0, ks=0;k<res;++k, kd+=3, ks+=4)
			{
				dstbuf[kd+0]=image->data[ks+0];
				dstbuf[kd+1]=image->data[ks+1];
				dstbuf[kd+2]=image->data[ks+2];
			}
			FILE *fdst=fopen(fn, "wb");
			if(!fdst)
			{
				LOG_WARNING("Cannot open \"%s\" for writing", fn);
				return -1;
			}
			fprintf(fdst, "P6\n%d %d\n255\n", image->iw, image->ih);
			fwrite(dstbuf, 1, usize, fdst);
			fclose(fdst);
			free(dstbuf);
			ret=0;
		}
		else if(ext_selection==7)//PGM
		{
			size_t res=(size_t)image->iw*image->ih, usize=res;
			unsigned char *dstbuf=(unsigned char*)malloc(usize);
			if(!dstbuf)
			{
				LOG_ERROR("Alloc error");
				return -1;
			}
			for(int k=0, ks=0;k<res;++k, ks+=4)
				dstbuf[k]=(
					+(int)(0.2126*0x1000+0.5)*image->data[ks+0]
					+(int)(0.7152*0x1000+0.5)*image->data[ks+1]
					+(int)(0.0722*0x1000+0.5)*image->data[ks+2]
					+0x800
				)>>12;
			FILE *fdst=fopen(fn, "wb");
			if(!fdst)
			{
				LOG_WARNING("Cannot open \"%s\" for writing", fn);
				return -1;
			}
			fprintf(fdst, "P5\n%d %d\n255\n", image->iw, image->ih);
			fwrite(dstbuf, 1, usize, fdst);
			fclose(fdst);
			free(dstbuf);
			ret=0;
		}
		else
			ret=save_media(fn, image, erroronfail);
		free(fn);
	}
	return ret;
}

static void print_version(char **dst, const char *end, const char *libname, int major, int minor, int patch, int isdll)
{
	if(*dst>=end)
		LOG_WARNING("Buffer overflow");
	*dst+=snprintf(*dst, end-*dst,
		"%-10s %u.%u.%u .%s\n",
		libname,
		major, minor, patch,
		isdll?"dll":"h"
	);
}
char* get_codecinfo(void)//don't forget to free(mem)
{
#define BUFLEN 8192
	char *str=(char*)malloc(BUFLEN);
	if(!str)
	{
		LOG_ERROR("Alloc error");
		return 0;
	}
	memset(str, 0, BUFLEN);
	char *ptr=str, *end=str+BUFLEN-1;
	int ver;
	
//	ptr+=snprintf(ptr, end-ptr, "FFmpeg:\n");
	ver=LIBAVCODEC_VERSION_INT;
	print_version(&ptr, end, "avcodec", ver>>16&255, ver>>8&255, ver&255, 0);
	ver=avcodec_version();
	print_version(&ptr, end, "avcodec", ver>>16&255, ver>>8&255, ver&255, 1);
	
	ver=LIBAVFORMAT_VERSION_INT;
	print_version(&ptr, end, "avformat", ver>>16&255, ver>>8&255, ver&255, 0);
	ver=avformat_version();
	print_version(&ptr, end, "avformat", ver>>16&255, ver>>8&255, ver&255, 1);
	
	ver=avutil_version();
	print_version(&ptr, end, "avutil", ver>>16&255, ver>>8&255, ver&255, 1);

	ver=LIBSWSCALE_VERSION_INT;
	print_version(&ptr, end, "swscale", ver>>16&255, ver>>8&255, ver&255, 0);
	ver=swscale_version();
	print_version(&ptr, end, "swscale", ver>>16&255, ver>>8&255, ver&255, 1);
	
	ptr+=snprintf(ptr, end-ptr, "\n");
	ver=LIBHEIF_NUMERIC_VERSION;//0xHHMMLL00
	print_version(&ptr, end, "libheif", ver>>24&255, ver>>16&255, ver>>8&255, 0);
	ver=heif_get_version_number();//0xHHMMLL00
	print_version(&ptr, end, "libheif", ver>>24&255, ver>>16&255, ver>>8&255, 1);
	
	ptr+=snprintf(ptr, end-ptr, "\n");
	ver=libraw_versionNumber();
	print_version(&ptr, end, "libraw", ver>>16&255, ver>>8&255, ver&255, 1);

	return str;
#undef BUFLEN
#if 0
	int printed=0;
//	printed+=snprintf(g_buf+printed, sizeof(g_buf)-1-printed, "FFmpeg:\n");
	//LIBAVCODEC_VERSION_INT
	unsigned ver=avcodec_version();
	printed+=snprintf(g_buf+printed, sizeof(g_buf)-1-printed,
		"%-10s %u.%u.%u\n",
		"avcodec", ver>>16&255, ver>>8&255, ver&255
	);
	ver=avformat_version();
	printed+=snprintf(g_buf+printed, sizeof(g_buf)-1-printed,
		"%-10s %u.%u.%u\n",
		"avformat", ver>>16&255, ver>>8&255, ver&255
	);
	ver=avutil_version();
	printed+=snprintf(g_buf+printed, sizeof(g_buf)-1-printed,
		"%-10s %u.%u.%u\n",
		"avutil", ver>>16&255, ver>>8&255, ver&255
	);
	ver=swscale_version();
	printed+=snprintf(g_buf+printed, sizeof(g_buf)-1-printed,
		"%-10s %u.%u.%u\n",
		"swscale", ver>>16&255, ver>>8&255, ver&255
	);

	ver=LIBHEIF_NUMERIC_VERSION;
	printed+=snprintf(g_buf+printed, sizeof(g_buf)-1-printed,
		"%-10s %u.%u.%u .h\n",
		"libheif", ver>>24&255, ver>>16&255, ver>>8&255
	);
	ver=heif_get_version_number();//0xHHMMLL00
	printed+=snprintf(g_buf+printed, sizeof(g_buf)-1-printed,
		"%-10s %u.%u.%u .dll\n",
		"libheif", ver>>24&255, ver>>16&255, ver>>8&255
	);
#endif
}