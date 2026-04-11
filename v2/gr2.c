#include"hView.h"
#include<stdint.h>
#include<stdio.h>
#include<stdlib.h>
static const char file[]=__FILE__;

#define CLAMP2(X, LO, HI)\
	do\
	{\
		if((X)<(LO))X=LO;\
		if((X)>(HI))X=HI;\
	}while(0)

#define SHIFT 19
#define PREDLIST\
	PRED(N+W-NW)\
	PRED(N)\
	PRED(W)\
	PRED(W+NE-N)\

enum
{
#define PRED(...) +1
	NPREDS=PREDLIST,
#undef  PRED
};
int gr2_save(const wchar_t *dstfn, short *image, int iw, int ih, int nlevels, char *bayermatrix)
{
	const int half=0;
//	int half=nlevels>>1;
	int64_t weights[NPREDS]={0};
	unsigned long long cache=0;
	int nbits=(int)sizeof(cache)<<3;
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
	for(int k=0;k<NPREDS;++k)
		weights[k]=(1<<SHIFT)/NPREDS;
	for(int ky=0, idx=0;ky<ih;++ky)
	{
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
				NE	=rows[1][0+1*2],
				NEEE	=rows[1][0+3*2],
				W	=rows[0][0-1*2],
				eNEEE	=rows[1][1+3*2],
				eW	=rows[0][1-1*2];
			int64_t pred, p0, estim[NPREDS];
			int vmax=N, vmin=W, curr, nbypass, sym, j;
			if(N<W)vmin=N, vmax=W;
			if(vmin>NE)vmin=NE;
			if(vmax<NE)vmax=NE;
			if(vmin>NEEE)vmin=NEEE;
			if(vmax<NEEE)vmax=NEEE;
			pred=1<<SHIFT>>1;
#define PRED(EXPR) estim[j]=EXPR; pred+=weights[j]*estim[j]; ++j;
			j=0;
			PREDLIST
#undef  PRED
			pred>>=SHIFT;
			p0=pred;
			CLAMP2(pred, vmin, vmax);

			nbypass=FLOOR_LOG2(eW+1);

			rows[0][0]=curr=image[idx]-half;
			sym=curr-(int)pred;
			sym=sym<<1^sym>>31;
			rows[0][1]=(2*eW+sym+eNEEE)>>2;
			{
				int e=(curr>p0)-(curr<p0);
#define PRED(EXPR) weights[j]+=e*estim[j]; ++j;
				j=0;
				PREDLIST
#undef  PRED
			}

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
		FILE *fdst=_wfopen(dstfn, L"wb");
		if(!fdst)
		{
			LOG_ERROR("Cannot open \"%s\" for writing", dstfn);
			return 1;
		}
		fprintf(fdst, "GR2 %d %d %d %c%c%c%c\n",
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
short* gr2_load(const wchar_t *srcfn, int *ret_iw, int *ret_ih, int *ret_nlevels, char *ret_bayermatrix)
{
	unsigned char *srcbuf=0;
	ptrdiff_t srcsize=get_filesizew(srcfn);
	if(srcsize<1)
	{
		//LOG_WARNING("Cannot open \"%s\"", srcfn);
		return 0;
	}
	{
		FILE *fsrc=_wfopen(srcfn, L"rb");
		if(!fsrc)
		{
			//LOG_WARNING("Cannot open \"%s\"", srcfn);
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
		if(memcmp(srcptr, "GR2 ", 4))
		{
			//LOG_WARNING("Invalid file");
			free(srcbuf);
			return 0;
		}
		srcptr+=4;
		while((unsigned)(*srcptr-'0')<10)
			iw=10*iw+*srcptr++-'0';
		if(*srcptr++!=' ')
		{
			//LOG_WARNING("Invalid file");
			free(srcbuf);
			return 0;
		}
		while((unsigned)(*srcptr-'0')<10)
			ih=10*ih+*srcptr++-'0';
		if(*srcptr++!=' ')
		{
			//LOG_WARNING("Invalid file");
			free(srcbuf);
			return 0;
		}
		while((unsigned)(*srcptr-'0')<10)
			nlevels=10*nlevels+*srcptr++-'0';
		if(*srcptr++!=' ')
		{
			//LOG_WARNING("Invalid file");
			free(srcbuf);
			return 0;
		}
		if(ret_bayermatrix)
			memcpy(ret_bayermatrix, srcptr, 4);
		srcptr+=4;
		if(*srcptr++!='\n')
		{
			//LOG_WARNING("Invalid file");
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
	
	int64_t weights[NPREDS]={0};
	unsigned long long *streamptr=(unsigned long long*)srcptr, cache=0;
	int nbits=0;
	for(int k=0;k<NPREDS;++k)
		weights[k]=(1<<SHIFT)/NPREDS;
	for(int ky=0, idx=0;ky<ih;++ky)
	{
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
				NE	=rows[1][0+1*2],
				NEEE	=rows[1][0+3*2],
				W	=rows[0][0-1*2],
				eNEEE	=rows[1][1+3*2],
				eW	=rows[0][1-1*2];
			int64_t pred, p0, estim[NPREDS];
			int vmax=N, vmin=W, curr, nbypass, sym, j;
			if(N<W)vmin=N, vmax=W;
			if(vmin>NE)vmin=NE;
			if(vmax<NE)vmax=NE;
			if(vmin>NEEE)vmin=NEEE;
			if(vmax<NEEE)vmax=NEEE;
			pred=1<<SHIFT>>1;
#define PRED(EXPR) estim[j]=EXPR; pred+=weights[j]*estim[j]; ++j;
			j=0;
			PREDLIST
#undef  PRED
			pred>>=SHIFT;
			p0=pred;
			CLAMP2(pred, vmin, vmax);

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

			curr=sym>>1^-(sym&1);
			curr+=(int)pred;
			image[idx]=curr+half;
			{
				int e=(curr>p0)-(curr<p0);
#define PRED(EXPR) weights[j]+=e*estim[j]; ++j;
				j=0;
				PREDLIST
#undef  PRED
			}
			rows[0][0]=curr;
			rows[0][1]=(2*eW+sym+eNEEE)>>2;

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
