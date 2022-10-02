//h_image.cpp - (most) image manipulation functions should be here
//Copyright (C) 2022  Ayman Wagih Mohsen
//
//This program is free software: you can redistribute it and/or modify
//it under the terms of the GNU General Public License as published by
//the Free Software Foundation, either version 3 of the License, or
//(at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program.  If not, see <https://www.gnu.org/licenses/>.

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#include		"hview.h"
#include		"generic.h"
short*			get_image()//delete[] the returned buffer
{
	if(!image)
		return nullptr;
	short *buffer=new short[image_size];
	int normal=1<<idepth;
	for(int k=0;k<image_size;++k)
		buffer[k]=(short)floor(image[k]*normal+0.5);
	return buffer;
}
void			set_image(short *src, int width, int height, int depth, ImageType type)
{
	imagetype=type;
	idepth=depth;
	iw=width, ih=height, image_size=iw*ih;
	float normal=1.f/((1<<depth)-1);
	switch(type)
	{
	case IM_GRAYSCALE:
	case IM_BAYER:
	case IM_BAYER_SEPARATE:
		image=(float*)realloc(image, image_size<<2);
		for(int k=0;k<image_size;++k)
			image[k]=src[k]*normal;
		break;
	case IM_RGBA:
		image=(float*)realloc(image, image_size<<4);//4 components/pixel
		for(int k=0;k<image_size<<2;++k)
			image[k]=src[k]*normal;
		break;
	}
	InvalidateRect(ghWnd, nullptr, true);
}
void			debayer()
{
	if(imagetype==IM_BAYER||imagetype==IM_BAYER_SEPARATE)
	{
		char bayer_idx[]={bayer[0]>>3, bayer[1]>>3, bayer[2]>>3, bayer[3]>>3};
		for(int k=0;k<4;++k)
		{
			if(bayer_idx[k]==2)
				bayer_idx[k]=0;
			else if(bayer_idx[k]==0)
				bayer_idx[k]=2;
		}
		int w2=iw;
		iw>>=1, ih>>=1, image_size=iw*ih;
		float *buffer=(float*)malloc(image_size*4*sizeof(float));
		memset(buffer, 0, image_size*4*sizeof(float));
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
		free(image);
		image=buffer;
		imagetype=IM_RGBA;
	}
	else if(imagetype==IM_RGBA)
	{
		bayer[0]=8, bayer[1]=0, bayer[2]=16, bayer[3]=8;
		char bayer_idx[]={bayer[0]>>3, bayer[1]>>3, bayer[2]>>3, bayer[3]>>3};
		for(int k=0;k<4;++k)
		{
			if(bayer_idx[k]==2)
				bayer_idx[k]=0;
			else if(bayer_idx[k]==0)
				bayer_idx[k]=2;
		}
		int w_2=iw, h_2=ih;
		iw<<=1, ih<<=1, image_size=iw*ih;
		float *buffer=(float*)malloc(image_size*sizeof(float));
		memset(buffer, 0, image_size*sizeof(float));
		for(int ky=0;ky<h_2;++ky)
		{
			for(int kx=0;kx<w_2;++kx)
			{
				int idx=(w_2*ky+kx)<<2;
				//buffer[idx+3]=1;//alpha
				buffer[iw* (ky<<1)   +(kx<<1)  ]+=image[idx+bayer_idx[0]];
				buffer[iw* (ky<<1)   +(kx<<1)+1]+=image[idx+bayer_idx[1]];
				buffer[iw*((ky<<1)+1)+(kx<<1)  ]+=image[idx+bayer_idx[2]];
				buffer[iw*((ky<<1)+1)+(kx<<1)+1]+=image[idx+bayer_idx[3]];
				//buffer[idx+1]*=0.5;//half green
			}
		}
		free(image);
		image=buffer;
		imagetype=IM_BAYER;
	}
}

void			reset_FFTW_state()
{
#ifdef FFTW3_H
	if(fft_w||fft_h)
	{
		int oldnplanes=1+3*(fft_type!=IM_GRAYSCALE);
		for(int k=0;k<oldnplanes;++k)
		{
			fftw_destroy_plan(fft_p[k]);
			fftw_destroy_plan(ifft_p[k]);
			fftw_free(fft_in_planes[k]);
			fftw_free(fft_out_planes[k]);
		}
	}
	FourierDomain=false;
	fft_w=0, fft_h=0;
	fft_type=IM_UNINITIALIZED;
#endif
}
void			applyFFT()
{
#ifdef FFTW3_H
	int nplanes=1+3*(imagetype!=IM_GRAYSCALE);
	if(fft_w!=iw||fft_h!=ih)//if uninitialized
	{
		reset_FFTW_state();
		//if(fft_w||fft_h)
		//{
		//	int oldnplanes=1+3*(fft_type!=IM_GRAYSCALE);
		//	for(int k=0;k<oldnplanes;++k)
		//	{
		//		fftw_destroy_plan(fft_p[k]);
		//		fftw_destroy_plan(ifft_p[k]);
		//		fftw_free(fft_in_planes[k]);
		//		fftw_free(fft_out_planes[k]);
		//	}
		//}
		fft_w=iw, fft_h=ih, fft_type=imagetype;
		int bytesize=image_size*sizeof(fftw_complex);
		bytesize>>=(imagetype!=IM_GRAYSCALE&&imagetype!=IM_RGBA)<<1;
		for(int kp=0;kp<nplanes;++kp)
		{
			fft_in_planes[kp]=(fftw_complex*)fftw_malloc(bytesize);
			fft_out_planes[kp]=(fftw_complex*)fftw_malloc(bytesize);
			if(!fft_in_planes[kp]||!fft_out_planes[kp])
			{
				reset_FFTW_state();
				return;
			}
			fft_p[kp]=fftw_plan_dft_2d(fft_h, fft_w, fft_in_planes[kp], fft_out_planes[kp], FFTW_FORWARD, FFTW_ESTIMATE);
			ifft_p[kp]=fftw_plan_dft_2d(fft_h, fft_w, fft_out_planes[kp], fft_in_planes[kp], FFTW_BACKWARD, FFTW_ESTIMATE);
		}
	}//end if uninitialized

	FourierDomain=!FourierDomain;//enter/exit the Fourier domain

	if(FourierDomain)//forward FFT
	{
		//initialize fft_in_planes
		if(imagetype==IM_GRAYSCALE)
		{
			auto in_plane=fft_in_planes[0];
			for(int ky=0;ky<ih;++ky)
			{
				for(int kx=0;kx<iw;++kx)
				{
					int idx=iw*ky+kx;
					in_plane[idx][0]=image[idx];
					in_plane[idx][1]=0;
				}
			}
		}
		else if(imagetype==IM_RGBA)
		{
			for(int ky=0;ky<ih;++ky)
			{
				for(int kx=0;kx<iw;++kx)
				{
					int idx=iw*ky+kx, s_idx=idx<<2;
					fft_in_planes[0][idx][0]=image[s_idx  ];//red
					fft_in_planes[0][idx][1]=0;
					fft_in_planes[1][idx][0]=image[s_idx+1];//green
					fft_in_planes[1][idx][1]=0;
					fft_in_planes[2][idx][0]=image[s_idx+2];//blue
					fft_in_planes[2][idx][1]=0;
					fft_in_planes[3][idx][0]=image[s_idx+3];//alpha
					fft_in_planes[3][idx][1]=0;
				}
			}
		}
		else if(imagetype==IM_BAYER||imagetype==IM_BAYER_SEPARATE)
		{
			int w2=iw>>1;
			for(int ky=0;ky<ih;ky+=2)
			{
				for(int kx=0;kx<iw;kx+=2)
				{
					int s_idx=iw*ky+kx, d_idx=w2*(ky>>1)+(kx>>1);
					fft_in_planes[0][d_idx][0]=image[s_idx     ];
					fft_in_planes[0][d_idx][1]=0;
					fft_in_planes[1][d_idx][0]=image[s_idx+1   ];
					fft_in_planes[1][d_idx][1]=0;
					fft_in_planes[2][d_idx][0]=image[s_idx+iw  ];
					fft_in_planes[2][d_idx][1]=0;
					fft_in_planes[3][d_idx][0]=image[s_idx+iw+1];
					fft_in_planes[3][d_idx][1]=0;
				}
			}
		}

		char halfsize=imagetype!=IM_GRAYSCALE&&imagetype!=IM_RGBA;
		int fft_w2=fft_w>>halfsize, fft_h2=fft_h>>halfsize, fft_size=fft_w2*fft_h2;
		for(int kp=0;kp<nplanes;++kp)//checkboard alternating sign to bring DC to center, forward FFT only
		{
			auto plane=fft_in_planes[kp];
			for(int ky=0;ky<fft_h2;++ky)
			{
				for(int kx=0;kx<fft_w2;++kx)
				{
					int idx=fft_w2*ky+kx, sign=1-((kx&1^ky&1)<<1);
					plane[idx][0]*=sign;
					plane[idx][1]*=sign;
				}
			}
		}

		for(int kp=0;kp<nplanes;++kp)//apply forward FFT
			fftw_execute(fft_p[kp]);

		double invsqrtsize=1/sqrt((double)fft_size);
		for(int kp=0;kp<nplanes;++kp)//normalize
		{
			auto plane=fft_out_planes[kp];
			for(int k=0;k<fft_size;++k)
			{
				plane[k][0]*=invsqrtsize;
				plane[k][1]*=invsqrtsize;
			}
		}

		double invhalfpi=2/M_PI;
		for(int kp=0;kp<nplanes;++kp)//write image to the volatile buffer fft_in_planes
		{
			auto srcbuf=fft_out_planes[kp], dstbuf=fft_in_planes[kp];
			for(int k=0;k<fft_size;++k)
			{
				auto &src=srcbuf[k], &dst=dstbuf[k];
				dst[0]=atan(10*sqrt(src[0]*src[0]+src[1]*src[1]))*invhalfpi;//complex magmitude mapped to [0~1]
				dst[1]=atan2(src[1], src[0]);//complex angle mapped to radians
			}
		}
	}
	else//backward FFT
	{
		for(int k=0;k<nplanes;++k)//apply inverse FFT
			fftw_execute(ifft_p[k]);

		char halfsize=imagetype!=IM_GRAYSCALE&&imagetype!=IM_RGBA;
		int fft_w2=fft_w>>halfsize, fft_h2=fft_h>>halfsize, fft_size=fft_w2*fft_h2;
		double invsqrtsize=1/sqrt((double)fft_size);
		for(int kp=0;kp<nplanes;++kp)//normalize
		{
			auto plane=fft_in_planes[kp];//note: the output of forward FFT is the input to backward FFT
			for(int k=0;k<fft_size;++k)
			{
				plane[k][0]*=invsqrtsize;
				plane[k][1]*=invsqrtsize;
			}
		}

		//write to image
		if(imagetype==IM_GRAYSCALE)
		{
			auto in_plane=fft_in_planes[0];
			for(int ky=0;ky<ih;++ky)
			{
				for(int kx=0;kx<iw;++kx)
				{
					int idx=iw*ky+kx;
					double re=in_plane[idx][0], im=in_plane[idx][1];
					image[idx]=(float)sqrt(re*re+im*im);

					//image[idx]=in_plane[idx][0];
				}
			}
		}
		else if(imagetype==IM_RGBA)
		{
			for(int ky=0;ky<ih;++ky)
			{
				for(int kx=0;kx<iw;++kx)
				{
					int idx=iw*ky+kx, s_idx=idx<<2;
					for(int kp=0;kp<4;++kp)
					{
						double
							re=fft_in_planes[kp][idx][0],
							im=fft_in_planes[kp][idx][1];
						image[idx+kp]=(float)sqrt(re*re+im*im);

						//image[idx+kp]=fft_in_planes[kp][idx][0];
					}
				}
			}
		}
		else if(imagetype==IM_BAYER||imagetype==IM_BAYER_SEPARATE)
		{
			int w2=iw>>1;
			int offsets[]={0, 1, iw, iw+1};
			for(int ky=0;ky<ih;ky+=2)
			{
				for(int kx=0;kx<iw;kx+=2)
				{
					int s_idx=iw*ky+kx, d_idx=w2*(ky>>1)+(kx>>1);
					for(int kp=0;kp<4;++kp)
					{
						double
							re=fft_in_planes[kp][d_idx][0],
							im=fft_in_planes[kp][d_idx][1];
						image[s_idx+offsets[kp]]=(float)sqrt(re*re+im*im);

						//image[s_idx+offsets[kp]]=fft_in_planes[kp][d_idx][0];
					}
				}
			}
		}
	}
#endif
}

void			calculate_histogram(const float *buffer, int width, int height, int xstride, int ystride, int nlevels, int *histogram, bool zerobuffer, int *histmax)
{
	int normal=nlevels-1;
	if(zerobuffer)
		memset(histogram, 0, nlevels<<2);
	for(int ky=0;ky<height;ky+=ystride)
	{
		auto row=buffer+width*ky;
		for(int kx=0;kx<width;kx+=xstride)
		{
			int level=int(row[kx]*normal);
			if(level<0)
				level=0;
			if(level>normal)
				level=normal;
			++histogram[level];
		}
	}
	if(histmax)
	{
		*histmax=0;
		for(int k=0;k<nlevels;++k)
			if(*histmax<histogram[k])
				*histmax=histogram[k];
	}
}
void			reset_histogram()
{
	if(histogram)
	{
		delete[] histogram;
		histogram=nullptr;
	}
}
void			toggle_histogram()
{
	histOn=!histOn;
	if(histOn)
	{
		int nlevels=1<<idepth;
		switch(imagetype)
		{
		case IM_GRAYSCALE:
			histogram=new int[nlevels];
			calculate_histogram(image, iw, ih, 1, 1, nlevels, histogram, true, &histmax_r);
			break;
		case IM_RGBA:
			histogram=new int[nlevels*3];
			calculate_histogram(image  , iw<<2, ih, 4, 1, nlevels, histogram, true, &histmax_r);
			calculate_histogram(image+1, iw<<2, ih, 4, 1, nlevels, histogram+nlevels, true, &histmax_g);
			calculate_histogram(image+2, iw<<2, ih, 4, 1, nlevels, histogram+(nlevels<<1), true, &histmax_b);
			break;
		case IM_BAYER:
		case IM_BAYER_SEPARATE:
			histogram=new int[nlevels*3];
			calculate_histogram(image, iw, ih, 2, 2, nlevels, histogram, true, nullptr);//TODO: support other Bayer matrices
			calculate_histogram(image+1, iw, ih, 2, 2, nlevels, histogram+nlevels, true, &histmax_r);
			calculate_histogram(image+iw, iw, ih, 2, 2, nlevels, histogram+(nlevels<<1), true, &histmax_b);
			calculate_histogram(image+iw+1, iw, ih, 2, 2, nlevels, histogram, false, &histmax_g);
			break;
		}
	}
	else
	{
		delete[] histogram;
		histogram=nullptr;
	}
}
void			cmd_histogram()
{
	int nlevels=1<<idepth;
	console_start(80, 1000+nlevels);
	if(imagetype==IM_GRAYSCALE)
	{
		int *histogram=new int[nlevels];
		calculate_histogram(image, iw, ih, 1, 1, nlevels, histogram, true, nullptr);
		print_histogram(histogram, nlevels, image_size, nullptr);
		delete[] histogram;
	}
	else
		printf("TODO: CMD histogram of non-grayscale images\n");

	console_pause();
	console_end();
}

const char		*chnames[]={"red", "green", "blue", "alpha"};
struct			Buffer
{
	int w, h;
	float *data;
};
int				copy_channel(int ch, Buffer &buf)//will debayer image if raw
{
	if(!image)
		return 0;
	switch(imagetype)
	{
	case IM_GRAYSCALE:
		buf.data=new float[image_size];
		if(!buf.data)
			return 0;
		buf.w=iw;
		buf.h=ih;
		memcpy(buf.data, image, image_size*sizeof(float));
		break;
	case IM_BAYER:case IM_BAYER_SEPARATE:
		debayer();
	case IM_RGBA:
		buf.data=new float[image_size];
		if(!buf.data)
			return 0;
		buf.w=iw;
		buf.h=ih;
		for(int k=0;k<image_size;++k)
			buf.data[k]=image[k<<2|ch&3];
		break;
	/*	{
			int w2=iw>>1, h2=ih>>1;
			buffer=new float[image_size>>2];
			if(!buffer)
				return nullptr;
			for(int ky=0;ky<h2;++ky)
			{
				for(int kx=0;kx<w2;++kx)
				{
					buffer[w2*ky+kx]=image[iw*(ky<<1|;
				}
			}
		}
		break;//*/
	}
	return 1;
}

struct			TransformPlan
{
	float *weights, *temp;
	int size;
};
void			apply_transform_1D(TransformPlan *p, const float *src, float *dst, int srcstride, int dststride)//SLOW O(n^2) DCT, src & dst can be the same pointer
{
	for(int k=0;k<p->size;++k)
	{
		auto row=p->weights+p->size*k;
		p->temp[k]=0;
		for(int ks=0, kd=0;kd<p->size;ks+=srcstride, ++kd)
			p->temp[k]+=row[kd]*src[ks];
	}
	for(int ks=0, kd=0;ks<p->size;++ks, kd+=dststride)
		dst[kd]=p->temp[ks];
}
void			finish_transform(TransformPlan *p)
{
	delete[] p->weights, p->weights=nullptr;
	delete[] p->temp, p->temp=nullptr;
}
void			init_dct(int size, TransformPlan *p)
{
	p->size=size;
	p->weights=new float[size*size];
	p->temp=new float[size];
	
	double scale=sqrt(2./size), freq=M_PI/size;
	for(int ky=0;ky<p->size;++ky)
	{
		auto row=p->weights+size*ky;
		for(int kx=0;kx<size;++kx)
			row[kx]=(float)(scale*cos(freq*(kx+0.5)*ky));
	}
}
void			init_idct(int size, TransformPlan *p)
{
	p->size=size;
	p->weights=new float[size*size];
	p->temp=new float[size];

	double scale=sqrt(2./size), freq=M_PI/size;
	for(int ky=0;ky<size;++ky)
	{
		auto row=p->weights+size*ky;
		for(int kx=0;kx<size;++kx)
		{
			if(!kx)
				row[kx]=(float)(scale*0.5);
			else
				row[kx]=(float)(scale*cos(freq*kx*(ky+0.5)));
		}
	}
}

float*			estimate_displacement(float *a, float *b, int bw, int bh, int *vec2)//delete[] returned buffer
{
	int size=bw*bh;
	if(!size)
		return 0;
	float *ta=new float[size], *tb=new float[size];
	TransformPlan p;

	init_dct(bw, &p);
	for(int ky=0;ky<bh;++ky)
	{
		apply_transform_1D(&p, a+bw*ky, ta+bw*ky, 1, 1);
		apply_transform_1D(&p, b+bw*ky, tb+bw*ky, 1, 1);
	}
	if(bw!=bh)
	{
		finish_transform(&p);
		init_dct(bh, &p);
	}
	for(int kx=0;kx<bw;++kx)
	{
		apply_transform_1D(&p, ta+kx, ta+kx, bw, bw);
		apply_transform_1D(&p, tb+kx, tb+kx, bw, bw);
	}
	finish_transform(&p);

	for(int k=0;k<size;++k)
		ta[k]*=tb[k];
	
	init_idct(bh, &p);
	for(int kx=0;kx<bw;++kx)
		apply_transform_1D(&p, ta+kx, tb+kx, bw, bw);
	if(bw!=bh)
	{
		finish_transform(&p);
		init_dct(bw, &p);
	}
	for(int ky=0;ky<bh;++ky)
		apply_transform_1D(&p, tb+bw*ky, tb+bw*ky, 1, 1);

	float vmin=tb[0], vmax=tb[0];
	for(int k=1;k<size;++k)
	{
		if(vmin>tb[k])
			vmin=tb[k];
		if(vmax<tb[k])
		{
			vmax=tb[k];
			vec2[0]=k%bw;
			vec2[1]=k/bw;
		}
	}
	if(vmin<vmax)
	{
		float gain=1.f/(vmax-vmin);
		for(int k=0;k<size;++k)
			tb[k]=(tb[k]-vmin)*gain;
	}
	delete[] ta;
	return tb;
}

void			pad_channel(float **buf, int bw, int bh, int bw2, int bh2)
{
	int s2=bw2*bh2;
	float *b2=new float[s2];
	memset(b2, 0, s2*sizeof(float));
	for(int ky=0;ky<bh2;++ky)
	{
		int kx0, ky0;

		ky0=ky-((bh2-bh)>>1);
		if(ky0>=0&&ky0<bh)
		{
			for(int kx=0;kx<bw2;++kx)
			{
				kx0=kx-((bw2-bw)>>1);
				if(ky0>=0&&ky0<bh)
					b2[bw2*ky+kx]=buf[0][bw*ky0+kx0];
			}
		}
	}

	delete[] *buf;
	*buf=b2;
}
void			set_channel(float *rgba, float *src, int bw, int bh, int *disp, int ch)
{
	for(int ky=0;ky<ih;++ky)
	{
		int kx0, ky0;

		ky0=ky+disp[1];
		if(ky0>=0&&ky0<bh)
		{
			for(int kx=0;kx<iw;++kx)
			{
				kx0=kx+disp[0];
				if(kx0>=0&&kx0<bw)
					rgba[4*(bw*ky+kx)+ch]=src[bw*ky0+kx0];
			}
		}
	}
}
void			mix_channels()
{
	Buffer channels[4]={0};
	int success=1, choice;

	//std::string str;
	console_start(80, 1000);
	printf("\nCHANNEL MIXER\n\n");
	for(int k=0;k<4;++k)
	{
		printf("Press any key  to open the source image for the %s channel\n", chnames[k]);
		if(k==3)
			printf("(Press Cancel if there is no alpha)\n");
		console_pause();
		success=open_media();
		if(!success)
		{
			if(k==3)
				success=1;
			break;
		}
		success=copy_channel(k, channels[k]);
		if(!success)
		{
			printf("Failed to extract %s channel. Aborting.", chnames[k]);
			console_pause();
			break;
		}
	}
	if(!success)
		goto cleanup;
	for(int k=1;k<4;++k)
	{
		if(channels[k].w!=channels->w||channels[k].h!=channels->h)
		{
			success='Y'|'D'<<8;
			break;
		}
	}
	if(success=='Y')
		printf("Dimension mismatch.\n");
	else
	{
		printf("Use auto-align only when channel contents are similar.\nAuto-align?  [0=Yes, otherwise No]  ");
		scanf("%d", &choice);
		choice=!choice;
	}
	
	int maxw, maxh;
	maxw=channels[0].w;
	maxh=channels[0].h;
	if(choice)//perform auto-align
	{
		if((success>>8&0xFF)=='D')//resize channels to same size
		{
			printf("Padding...\n");
			for(int k=0;k<4;++k)
			{
				if(maxw<channels[k].w)
					maxw=channels[k].w;
				if(maxh<channels[k].h)
					maxh=channels[k].h;
			}
			for(int k=0;k<4;++k)
			{
				if(channels[k].data)
				{
					pad_channel(&channels[k].data, channels[k].w, channels[k].h, maxw, maxh);
					channels[k].w=maxw;
					channels[k].h=maxh;
				}
				else
				{
					channels[k].data=nullptr;
					channels[k].w=0;
					channels[k].h=0;
				}
			}
		}
		int disp[2]={0};
		imagetype=IM_RGBA;
		iw=maxw, ih=maxh, image_size=iw*ih;
		image=(float*)realloc(image, image_size*4*sizeof(float));
		//memset(image, 0, image_size*4*sizeof(float));
		for(int k=0;k<image_size;++k)//set alpha to 1
		{
			image[k<<2]=0;
			image[k<<2|1]=0;
			image[k<<2|2]=0;
			image[k<<2|3]=1;
		}
		set_channel(image, channels[0].data, iw, ih, disp, 0);
		for(int k=1;k<3;++k)
		{
			printf("Estimating %s-on-red displacement...\n", chnames[k]);
			float *diff=estimate_displacement(channels[0].data, channels[1].data, maxw, maxh, disp);
			if(!diff)
				printf("Failed to retrieve displacement buffer\n");
			ImageType it2=IM_GRAYSCALE;
			std::swap(image, diff), std::swap(imagetype, it2);
			render();
			std::swap(image, diff), std::swap(imagetype, it2);
			printf("The %s channel must be displaced by (%d, %d) to fit on red channel.\n\t[0=Correct, otherwise Incorrect]  ", chnames[k], disp[0], disp[1]);
			scanf("%d", &choice);
			if(choice)
			{
				printf("Enter a better estimate:\n\tdx: ");
				scanf("%d", disp);
				printf("\tdy: ");
				scanf("%d", disp+1);
			}

			set_channel(image, channels[k].data, iw, ih, disp, k);
			render();

			delete[] diff;
		}
		if(channels[3].data)
		{
			disp[0]=disp[1]=0;
			set_channel(image, channels[3].data, iw, ih, disp, 3);//set alpha
		}
	}
	else//stack as is
	{
		int disp[2]={0};
		imagetype=IM_RGBA;
		iw=maxw, ih=maxh, image_size=iw*ih;
		//image=(float*)realloc(image, image_size*4*sizeof(float));
		for(int k=0;k<image_size;++k)//set alpha to 1
		{
			image[k<<2]=0;
			image[k<<2|1]=0;
			image[k<<2|2]=0;
			image[k<<2|3]=1;
		}
		memset(image, 0, image_size*4*sizeof(float));
		for(int k=0;k<4;++k)
		{
			set_channel(image, channels[k].data, iw, ih, disp, k);
			render();
		}
	}
	
	printf("Done.\n");
cleanup:
	console_pause();
	console_end();
	for(int k=0;k<4;++k)
		delete[] channels[k].data;
}