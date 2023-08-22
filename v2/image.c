#include"hView.h"
#include<stdlib.h>
#include<string.h>
static const char file[]=__FILE__;

static void copy_row(unsigned char *dst, int dstpxoffset, int dstdepth, const unsigned char *src, int srcpxoffset, int srcdepth, int npx)
{
	if(srcdepth==dstdepth)
	{
		int idxshift=dstdepth==16?3:2;
		memcpy(dst+(dstpxoffset<<idxshift), src+(srcpxoffset<<idxshift), npx<<idxshift);
	}
	else
	{
		int srcstride=srcdepth==16?2:1, dststride=dstdepth==16?2:1;
		const unsigned char *srcptr=src+(srcpxoffset<<(srcdepth==16?3:2));
		unsigned char *dstptr=dst+(dstpxoffset<<(dstdepth==16?3:2));
		if(srcstride==2)
		{
			++srcptr;//skip low byte
			for(int k=0;k<(npx<<2);++k)
			{
				*dstptr=*srcptr;
				++dstptr;
				srcptr+=2;
			}
		}
		else
		{
			for(int k=0;k<(npx<<2);++k)
			{
				dstptr[0]=0;

				dstptr[1]=*srcptr;
				dstptr+=2;
				++srcptr;
			}
		}
	}
}
void image_blit(ImageHandle dst, int x, int y, const unsigned char *src, int iw, int ih, int srcdepth)
{
	int srcx1=0, srcx2=iw,
		srcy1=0, srcy2=ih;
	int dstx1=x, dstx2=x+iw,
		dsty1=y, dsty2=y+ih;
	if(dstx1<0)srcx1-=dstx1, dstx1=0;//crop left
	if(dsty1<0)srcy1-=dsty1, dsty1=0;//crop top
	if(dstx2>dst->iw)srcx2-=dst->iw-dstx2, dstx2=dst->iw;//crop right
	if(dsty2>dst->ih)srcy2-=dst->ih-dsty2, dsty2=dst->ih;//crop bottom
	if(dstx1<dstx2&&dsty1<dsty2)
	{
		for(int srcy=srcy1, dsty=dsty1;srcy<srcy2;++srcy, ++dsty)
			copy_row(dst->data, dst->iw*dsty+dstx1, dst->depth, src, iw*srcy+srcx1, srcdepth, dstx2-dstx1);
			//memcpy(dst->data+((dst->iw*dsty+dstx1)<<idxshift), src+((iw*srcy+srcx1)<<idxshift), (size_t)(dstx2-dstx1)<<idxshift);
	}
}
ImageHandle image_construct(int xcap, int ycap, int dstdepth, const unsigned char *src, int iw, int ih, int srcdepth)
{
	ptrdiff_t res;
	ImageHandle image;
	int idxshift=dstdepth==16?3:2;

	if(!xcap)
		xcap=iw;
	if(!ycap)
		ycap=ih;
	res=(ptrdiff_t)xcap*ycap;
	image=(ImageHandle)malloc(sizeof(ImageHeader)+(res<<idxshift));
	if(!image)
	{
		LOG_ERROR("Allocation error");
		return 0;
	}
	image->xcap=xcap;
	image->ycap=ycap;
	image->iw=iw;
	image->ih=ih;
	image->depth=dstdepth;
	image->reserved0=0;
	if(src)
		image_blit(image, 0, 0, src, iw, ih, srcdepth);
	else
		memset(image->data, 0, res<<idxshift);
	return image;
}
void image_free(ImageHandle *image)
{
	free(*image);
	*image=0;
}
void image_resize(ImageHandle *image, int w, int h)
{
	ImageHandle im2=image_construct(0, 0, image[0]->depth, 0, w, h, image[0]->depth);
	image_blit(im2, 0, 0, image[0]->data, image[0]->iw, image[0]->ih, image[0]->depth);
	image_free(image);
	*image=im2;
}
