#include		"hview.h"
/*void			separate_bayer()
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
}//*/
void			debayer()
{
	char bayer_idx[]={bayer[0]>>3, bayer[1]>>3, bayer[2]>>3, bayer[3]>>3};
	int w2=iw;
	iw>>=1, ih>>=1, image_size=iw*ih;
	float *buffer=(float*)malloc(image_size*4*sizeof(float));
	memset(buffer, 0, image_size*4*sizeof(float));
	if(imagetype==IM_BAYER||imagetype==IM_BAYER_SEPARATE)
	//if(imagetype==IM_BAYER)
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
	//else if(imagetype==IM_BAYER_SEPARATE)
	//{
	//	for(int ky=0;ky<ih;++ky)
	//	{
	//		for(int kx=0;kx<iw;++kx)
	//		{
	//			int idx=(iw*ky+kx)<<2;
	//			buffer[idx+3]=1;//alpha
	//			buffer[idx+bayer_idx[0]]+=image[w2*ky+kx];
	//			buffer[idx+bayer_idx[1]]+=image[w2*ky+kx+iw];
	//			buffer[idx+bayer_idx[2]]+=image[w2*(ky+ih)+kx];
	//			buffer[idx+bayer_idx[3]]+=image[w2*(ky+ih)+kx+iw];
	//			buffer[idx+1]*=0.5;//half green
	//		}
	//	}
	//}
	free(image);
	image=buffer;
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
					image[idx]=sqrt(re*re+im*im);

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
						image[idx+kp]=sqrt(re*re+im*im);

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
						image[s_idx+offsets[kp]]=sqrt(re*re+im*im);

						//image[s_idx+offsets[kp]]=fft_in_planes[kp][d_idx][0];
					}
				}
			}
		}
	}
#endif
}