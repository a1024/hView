#include"hView.h"
#include<stdlib.h>
#include<string.h>
static const char file[]=__FILE__;

Image8* image_alloc8(const unsigned char *src, int iw, int ih, int nch, int srcdepth)
{
	ptrdiff_t size=(ptrdiff_t)iw*ih*nch*sizeof(char);
	Image8 *image=(Image8*)malloc(sizeof(Image8)+size);
	if(!image)
	{
		LOG_ERROR("Alloc error");
		return 0;
	}
	image->iw=iw;
	image->ih=ih;
	image->nch=nch;
	image->depth=srcdepth;
	image->srcdepth=srcdepth;
	if(src)
		memcpy(image->data, src, size);
	else
		memset(image->data, 0, size);
	return image;
}
Image16* image_alloc16(const unsigned short *src, int iw, int ih, int srcnch, int nch, int nlevels0, int depth)
{
	ptrdiff_t size=(ptrdiff_t)iw*ih*nch*sizeof(short);
	Image16 *image=(Image16*)malloc(sizeof(Image16)+size);
	if(!image)
	{
		LOG_ERROR("Alloc error");
		return 0;
	}
	image->iw=iw;
	image->ih=ih;
	image->srcnch=srcnch;
	image->nch=nch;
	image->nlevels0=nlevels0;
	image->depth=depth;
	if(src)
		memcpy(image->data, src, size);
	else
		memset(image->data, 0, size);
	return image;
}
void image_free(void *pimage)
{
	free(*(void**)pimage);
	*(void**)pimage=0;
}
void image_export(Image8 *dst, const Image16 *src, int imagetype)
{
	CLAMP2(brightness, 0, imagedepth-8);
	if(!dst||!src)
	{
		LOG_ERROR("image_export  src %zd  dst %zd", src, dst);
		return;
	}
	const unsigned short *srcptr=src->data;
	unsigned char *dstptr=dst->data;
	switch(imagetype)
	{
	case IM_GRAYSCALEv2:
		{
			if(src->nch!=1||dst->nch!=4||dst->iw!=src->iw||dst->ih!=src->ih||src->nch!=1)
			{
				LOG_WARNING("Dimension mismatch  CWH src %d*%d*%d vs dst %d*%d*%d",
					src->nch, src->iw, src->ih,
					dst->nch, dst->iw, dst->ih
				);
				return;
			}
			ptrdiff_t res=(ptrdiff_t)src->iw*src->ih;
			int sh=src->depth-8;
			for(ptrdiff_t k=0;k<res;++k)
			{
				int val=*srcptr++<<brightness>>sh;
				CLAMP2(val, 0, 255);
				*dstptr++=val;
				*dstptr++=val;
				*dstptr++=val;
				*dstptr++=255;
			}
		}
		break;
	case IM_RGBA:
		{
			if(dst->nch!=src->nch||dst->iw!=src->iw||dst->ih!=src->ih||(src->nch!=3&&src->nch!=4))
			{
				LOG_WARNING("Dimension mismatch  CWH src %d*%d*%d vs dst %d*%d*%d",
					src->nch, src->iw, src->ih,
					dst->nch, dst->iw, dst->ih
				);
				return;
			}
			ptrdiff_t res=(ptrdiff_t)src->nch*src->iw*src->ih;
			int sh=src->depth-8;
			for(ptrdiff_t k=0;k<res;++k)
			{
				int val=srcptr[k]<<brightness>>sh;
				CLAMP2(val, 0, 255);
				dstptr[k]=val;
			}
		}
		break;
	case IM_BAYERv2:
		{
			if(dst->iw*2!=src->iw||dst->ih*2!=src->ih||src->nch!=1||(dst->nch!=3&&dst->nch!=4))
			{
				LOG_WARNING("Dimension mismatch  CWH src %d*%d*%d vs dst %d * %d*2 * %d*2",
					src->nch, src->iw, src->ih,
					dst->nch, dst->iw, dst->ih
				);
				return;
			}
			int sh=src->depth-8;
			ptrdiff_t idx=0;
			for(int ky=0;ky<dst->ih;++ky)
			{
				for(int kx=0;kx<dst->iw;++kx, idx+=4)
				{
					int v0=srcptr[src->iw*(ky*2+0)+kx*2+0];//FIXME Bayer matrix
					int v1=srcptr[src->iw*(ky*2+0)+kx*2+1];
					int v2=srcptr[src->iw*(ky*2+1)+kx*2+0];
					int v3=srcptr[src->iw*(ky*2+1)+kx*2+1];
					int rgb[3]={0};
					//rgb[bayer[0]]+=v0;		//green tint
					//if(!rgb[bayer[1]])rgb[bayer[1]]+=v1;
					//if(!rgb[bayer[2]])rgb[bayer[2]]+=v2;
					//if(!rgb[bayer[2]])rgb[bayer[3]]+=v3;
					//rgb[0]>>=sh;
					//rgb[1]>>=sh;
					//rgb[2]>>=sh;
					rgb[(int)bayer[0]]+=v0;//green tint
					rgb[(int)bayer[1]]+=v1;
					rgb[(int)bayer[2]]+=v2;
					rgb[(int)bayer[3]]+=v3;
					rgb[0]<<=brightness;
					rgb[1]<<=brightness;
					rgb[2]<<=brightness;
					rgb[0]>>=sh;
					rgb[1]>>=sh+1;
					rgb[2]>>=sh;
					CLAMP2(rgb[0], 0, 255);
					CLAMP2(rgb[1], 0, 255);
					CLAMP2(rgb[2], 0, 255);
					dstptr[idx+0]=rgb[0];
					dstptr[idx+1]=rgb[1];
					dstptr[idx+2]=rgb[2];
					dstptr[idx+3]=255;
				}
			}
#if 0
			if(*(int*)bayer==(1<<24|2<<16|0<<8|1))//GRBG
			{
				for(int ky=0;ky<dst->ih;++ky)
				{
					for(int kx=0;kx<dst->iw;++kx, idx+=4)
					{
						int g0=srcptr[src->iw*(ky*2+0)+kx*2+0];//FIXME Bayer matrix
						int r0=srcptr[src->iw*(ky*2+0)+kx*2+1];
						int b0=srcptr[src->iw*(ky*2+1)+kx*2+0];
						int g1=srcptr[src->iw*(ky*2+1)+kx*2+1];
						r0>>=sh;
						g0=(g0+g1)>>(sh+1);
						b0>>=sh;
						dstptr[idx+0]=r0;
						dstptr[idx+1]=g0;
						dstptr[idx+2]=b0;
						dstptr[idx+3]=255;
					}
				}
			}
			else if(*(int*)bayer==(2<<24|1<<16|1<<8|0))//RGGB
			{
				for(int ky=0;ky<dst->ih;++ky)
				{
					for(int kx=0;kx<dst->iw;++kx, idx+=4)
					{
						int r0=srcptr[src->iw*(ky*2+0)+kx*2+0];//FIXME Bayer matrix
						int g0=srcptr[src->iw*(ky*2+0)+kx*2+1];
						int g1=srcptr[src->iw*(ky*2+1)+kx*2+0];
						int b0=srcptr[src->iw*(ky*2+1)+kx*2+1];
						r0>>=sh;
						g0=(g0+g1)>>(sh+1);
						b0>>=sh;
						dstptr[idx+0]=r0;
						dstptr[idx+1]=g0;
						dstptr[idx+2]=b0;
						dstptr[idx+3]=255;
					}
				}
			}
			else
			{
				LOG_WARNING("Bayer matrix %d%d%d%d", bayer[0], bayer[1], bayer[2], bayer[3]);
				return;
			}
#endif
		}
		break;
	}
}

void image_inplacexflip(Image16 *src, char *bayer)
{
	int rowstride=src->nch*src->iw;
	for(int ky=0;ky<src->ih;++ky)
	{
		unsigned short *pfwd=src->data+rowstride*ky, *pbwd=pfwd+rowstride-src->nch;
		while(pfwd<pbwd)
		{
			for(int kc=0;kc<src->nch;++kc)
			{
				int temp;
				SWAPVAR(pfwd[kc], pbwd[kc], temp);
			}
			pfwd+=src->nch;
			pbwd-=src->nch;
		}
	}
	if(bayer)
	{
		char temp;
		SWAPVAR(bayer[0], bayer[1], temp);
		SWAPVAR(bayer[2], bayer[3], temp);
	}
}
void image_inplaceyflip(Image16 *src, char *bayer)
{
	int rowstride=src->nch*src->iw;
	ptrdiff_t size=(ptrdiff_t)rowstride*src->ih;
	unsigned short *pfwd=src->data, *pbwd=pfwd+size-rowstride;
	while(pfwd<pbwd)
	{
		unsigned short *pfwd2=pfwd, *pbwd2=pbwd;
		for(int kx=0;kx<rowstride;++kx)
		{
			int a=*pfwd2, b=*pbwd2;
			*pfwd2++=b;
			*pbwd2++=a;
		}
		pfwd+=rowstride;
		pbwd-=rowstride;
	}
	if(bayer)
	{
		char temp;
		SWAPVAR(bayer[0], bayer[2], temp);
		SWAPVAR(bayer[1], bayer[3], temp);
	}
}
void image_transpose(Image16 **src, char *bayer)
{
	Image16 *dst=image_alloc16(0, src[0]->ih, src[0]->iw, src[0]->srcnch, src[0]->nch, src[0]->nlevels0, src[0]->depth);
	dst->depth=src[0]->depth;
	for(int ky=0;ky<src[0]->ih;++ky)
	{
		for(int kx=0;kx<src[0]->iw;++kx)
		{
			const unsigned short *srcptr=src[0]->data+src[0]->nch*((ptrdiff_t)src[0]->iw*ky+kx);
			unsigned short *dstptr=dst->data+dst->nch*((ptrdiff_t)dst->iw*kx+ky);
			for(int kc=0;kc<src[0]->nch;++kc)
				*dstptr++=*srcptr++;
		}
	}
	free(*src);
	*src=dst;
	if(bayer)
	{
		char temp;
		SWAPVAR(bayer[1], bayer[2], temp);
	}
}
