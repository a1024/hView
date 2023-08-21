#include"hView.h"
#include<stdlib.h>
#include<string.h>
static const char file[]=__FILE__;

void image_blit(ImageHandle dst, int x, int y, unsigned char *src, int iw, int ih)
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
			memcpy(dst->data+((dst->iw*dsty+dstx1)<<2), src+((iw*srcy+srcx1)<<2), (size_t)(dstx2-dstx1)<<2);
	}
}
ImageHandle image_construct(int xcap, int ycap, unsigned char *src, int iw, int ih)
{
	ptrdiff_t res;
	ImageHandle image;

	if(!xcap)
		xcap=iw;
	if(!ycap)
		ycap=ih;
	res=(ptrdiff_t)xcap*ycap;
	image=(ImageHandle)malloc(sizeof(ImageHeader)+(res<<2));
	if(!image)
	{
		LOG_ERROR("Allocation error");
		return 0;
	}
	image->xcap=xcap;
	image->ycap=ycap;
	image->iw=iw;
	image->ih=ih;
	if(src)
		image_blit(image, 0, 0, src, iw, ih);
	else
		memset(image->data, 0, res<<2);
	return image;
}
void image_free(ImageHandle *image)
{
	free(*image);
	*image=0;
}
void image_resize(ImageHandle *image, int w, int h)
{
	ImageHandle im2=image_construct(0, 0, 0, w, h);
	image_free(image);
	*image=im2;
}
