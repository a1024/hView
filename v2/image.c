#include"hView.h"
#include<stdlib.h>
#include<string.h>
static const char file[]=__FILE__;

static void copy_row(unsigned char *dst, int dstpxoffset, int dstdepth, const unsigned char *src, int srcpxoffset, int srcdepth, int npx)
{
	if(srcdepth==dstdepth)
	{
		int idxshift=dstdepth==16?3:2;//16-bit -> 8 bytes, else 4 bytes
		memcpy(dst+((size_t)dstpxoffset<<idxshift), src+((size_t)srcpxoffset<<idxshift), (size_t)npx<<idxshift);
	}
	else
	{
		int srcstride=srcdepth==16?2:1, dststride=dstdepth==16?2:1;
		const unsigned char *srcptr=src+((size_t)srcpxoffset<<(srcdepth==16?3:2));
		unsigned char *dstptr=dst+((size_t)dstpxoffset<<(dstdepth==16?3:2));
		if(srcstride==2)//16 -> 8 bit
		{
			++srcptr;//skip low byte
			for(int k=0;k<(npx<<2);++k)
			{
				*dstptr=*srcptr;
				++dstptr;
				srcptr+=2;
			}
		}
		else//8 -> 16 bit
		{
			for(int k=0;k<(npx<<2);++k)
			{
				if((k&3)==3&&*srcptr==0xFF)
					dstptr[0]=0xFF;
				else
					dstptr[0]=0;

				dstptr[1]=*srcptr;
				dstptr+=2;
				++srcptr;
			}
		}
	}
}
void image_blit(ImageHandle dst, int x, int y, const unsigned char *src, int iw, int ih, int rowpad, int srcdepth)
{
	int rowlen=iw+rowpad;
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
			copy_row(dst->data, dst->xcap*dsty+dstx1, dst->depth, src, rowlen*srcy+srcx1, srcdepth, dstx2-dstx1);
			//memcpy(dst->data+((dst->iw*dsty+dstx1)<<idxshift), src+((iw*srcy+srcx1)<<idxshift), (size_t)(dstx2-dstx1)<<idxshift);//X
	}
}
void image_export_rgb8(ImageHandle dst, ImageHandle src, int imagetype)
{
	if(src->iw!=dst->iw||src->ih!=dst->ih)
	{
		LOG_WARNING("Dimension mismatch WH %d*%d %d*%d", src->iw, src->ih, dst->iw, dst->ih);
		return;
	}
	if(imagetype==IM_BAYER&&debayer_on)//superpixel (simplest)
	{
		unsigned short *srcptr=(unsigned short*)src->data;
		char debayer[4]={0};//RGGB
		for(int k=0;k<4;++k)
		{
			if(bayer[k]==0)
			{
				debayer[0]=k;
				break;
			}
		}
		for(int k=0, kd=1;k<4;++k)
		{
			if(bayer[k]==1)
				debayer[kd++]=k;
		}
		for(int k=0;k<4;++k)
		{
			if(bayer[k]==2)
			{
				debayer[3]=k;
				break;
			}
		}
		for(int ky=0;ky<src->ih;ky+=2)
		{
			for(int kx=0;kx<src->iw;kx+=2)
			{
				//if(ky==2016&&kx==1588)//
				//	printf("");

				int comp[]=
				{
					srcptr[(src->iw*(ky+0)+kx+0)<<2|bayer[0]],
					srcptr[(src->iw*(ky+0)+kx+1)<<2|bayer[1]],
					srcptr[(src->iw*(ky+1)+kx+0)<<2|bayer[2]],
					srcptr[(src->iw*(ky+1)+kx+1)<<2|bayer[3]],
				};
				int rggb[]=
				{
					comp[debayer[0]],
					comp[debayer[1]],
					comp[debayer[2]],
					comp[debayer[3]],
				};
				int idx;

				idx=(dst->iw*(ky+0)+kx+0)<<2;
				dst->data[idx|0]=rggb[0]>>8;
				dst->data[idx|1]=(rggb[1]+rggb[2])>>9;
				dst->data[idx|2]=rggb[3]>>8;
				dst->data[idx|3]=255;

				idx=(dst->iw*(ky+0)+kx+1)<<2;
				dst->data[idx|0]=rggb[0]>>8;
				dst->data[idx|1]=(rggb[1]+rggb[2])>>9;
				dst->data[idx|2]=rggb[3]>>8;
				dst->data[idx|3]=255;

				idx=(dst->iw*(ky+1)+kx+0)<<2;
				dst->data[idx|0]=rggb[0]>>8;
				dst->data[idx|1]=(rggb[1]+rggb[2])>>9;
				dst->data[idx|2]=rggb[3]>>8;
				dst->data[idx|3]=255;

				idx=(dst->iw*(ky+1)+kx+1)<<2;
				dst->data[idx|0]=rggb[0]>>8;
				dst->data[idx|1]=(rggb[1]+rggb[2])>>9;
				dst->data[idx|2]=rggb[3]>>8;
				dst->data[idx|3]=255;
			}
		}
	}
	else
		image_blit(dst, 0, 0, src->data, src->iw, src->ih, src->xcap-src->iw, src->depth);
}
ImageHandle image_construct(int xcap, int ycap, int dstdepth, const unsigned char *src, int iw, int ih, int rowpad, int srcdepth)
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
	image->srcdepth=srcdepth;
	if(src)
		image_blit(image, 0, 0, src, iw, ih, rowpad, srcdepth);
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
	ImageHandle im2=image_construct(0, 0, image[0]->depth, 0, w, h, 0, image[0]->depth);
	image_blit(im2, 0, 0, image[0]->data, image[0]->iw, image[0]->ih, image[0]->xcap-image[0]->iw, image[0]->depth);
	image_free(image);
	*image=im2;
}
