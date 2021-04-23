#include		"hview.h"
void			separate_bayer()
{
	float *buffer=(float*)malloc(image_size<<2);
	int w2=iw>>1, h2=ih>>1;
	for(int ky=0;ky<h2;++ky)
	{
		int iy=ky<<1;
		for(int kx=0;kx<w2;++kx)
		{
			int ix=kx<<1;
			buffer[iw*ky+kx]=image[iw*iy+ix];
			buffer[iw*ky+kx+w2]=image[iw*iy+ix+1];
			buffer[iw*(ky+h2)+kx]=image[iw*(iy+1)+ix];
			buffer[iw*(ky+h2)+kx+w2]=image[iw*(iy+1)+ix+1];
		}
	}
	free(image);
	image=buffer;
}
void			regroup_bayer()
{
	float *buffer=(float*)malloc(image_size<<2);
	int w2=iw>>1, h2=ih>>1;
	for(int ky=0;ky<h2;++ky)
	{
		int iy=ky<<1;
		for(int kx=0;kx<w2;++kx)
		{
			int ix=kx<<1;
			buffer[iw*iy+ix]=image[iw*ky+kx];
			buffer[iw*iy+ix+1]=image[iw*ky+kx+w2];
			buffer[iw*(iy+1)+ix]=image[iw*(ky+h2)+kx];
			buffer[iw*(iy+1)+ix+1]=image[iw*(ky+h2)+kx+w2];
		}
	}
	free(image);
	image=buffer;
}
void			debayer()
{
	char bayer_idx[]={bayer[0]>>3, bayer[1]>>3, bayer[2]>>3, bayer[3]>>3};
	int w2=iw;
	iw>>=1, ih>>=1, image_size=iw*ih;
	float *buffer=(float*)malloc(image_size*4*sizeof(float));
	memset(buffer, 0, image_size*4*sizeof(float));
	if(imagetype==IM_BAYER)
	{
		for(int ky=0;ky<ih;++ky)
		{
			for(int kx=0;kx<iw;++kx)
			{
				int idx=(iw*ky+kx)<<2;
				buffer[idx+3]=1;//alpha
				buffer[idx+bayer_idx[0]]+=image[w2*(ky<<1)+(kx<<1)];
				buffer[idx+bayer_idx[1]]+=image[w2*(ky<<1)+(kx<<1)+1];
				buffer[idx+bayer_idx[2]]+=image[w2*((ky<<1)+1)+(kx<<1)];
				buffer[idx+bayer_idx[3]]+=image[w2*((ky<<1)+1)+(kx<<1)+1];
				buffer[idx+1]*=0.5;//half green
			}
		}
	}
	else if(imagetype==IM_BAYER_SEPARATE)
	{
		for(int ky=0;ky<ih;++ky)
		{
			for(int kx=0;kx<iw;++kx)
			{
				int idx=(iw*ky+kx)<<2;
				buffer[idx+3]=1;//alpha
				buffer[idx+bayer_idx[0]]+=image[w2*ky+kx];
				buffer[idx+bayer_idx[1]]+=image[w2*ky+kx+iw];
				buffer[idx+bayer_idx[2]]+=image[w2*(ky+ih)+kx];
				buffer[idx+bayer_idx[3]]+=image[w2*(ky+ih)+kx+iw];
				buffer[idx+1]*=0.5;//half green
			}
		}
	}
	free(image);
	image=buffer;
}