#include"hView.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<math.h>
#include<immintrin.h>
#define printf console_log
#define AC_IMPLEMENTATION
#include"ac.h"
static const char file[]=__FILE__;


void bayer_pack(const unsigned short *src, int iw, int ih, char *bayer, unsigned short *dst)
{
	for(int ky=0;ky<ih;++ky)
	{
		for(int kx=0;kx<iw;++kx)
		{
			int idx=iw*ky+kx;
			dst[idx]=src[idx<<2|bayer[(ky&1)<<1|kx&1]];
		}
	}
}
void bayer_unpack(const unsigned short *src, int iw, int ih, char *bayer, unsigned short *dst)
{
	memset(dst, 0, iw*ih*sizeof(short[4]));
	for(int ky=0;ky<ih;++ky)
	{
		for(int kx=0;kx<iw;++kx)
		{
			int idx=iw*ky+kx;
			dst[idx<<2|bayer[(ky&1)<<1|kx&1]]=dst[idx];
			dst[idx<<2|3]=0xFFFF;
		}
	}
}
void compare_bufs_u16(unsigned short *b1, unsigned short *b0, int iw, int ih, int pxcomps, int stride, const char *name, int backward)
{
	ptrdiff_t len=(ptrdiff_t)stride*iw*ih;
	int inc=stride*(1-(backward<<1));
	for(ptrdiff_t k=backward?len-stride:0;k>=0&&k<len;k+=inc)
	{
		if(memcmp(b1+k, b0+k, pxcomps*sizeof(short)))
		{
			ptrdiff_t idx=k/stride, kx=idx%iw, ky=idx/iw;
			printf("%s error XY (%5lld, %5lld) / %5d x %5d  b1 != b0\n", name, kx, ky, iw, ih);
			for(int kc=0;kc<pxcomps;++kc)
				printf("C%d  0x%02X != 0x%02X    %d != %d\n", kc, (unsigned)b1[k+kc], (unsigned)b0[k+kc], (unsigned)b1[k+kc], (unsigned)b0[k+kc]);
			return;
		}
	}
	printf("%s:\tSUCCESS\n", name);
}

void bufadd_i16(short *buf, int count, int ammount)
{
	for(int k=0;k<count;++k)
		buf[k]+=ammount;
}
void raw_ycmcb_fwd(short *buf, int iw, int ih, int idepth)
{
	int mask=((1<<idepth)-1)<<(16-idepth);
	for(int ky=0;ky<ih;ky+=2)
	{
		for(int kx=0;kx<iw;kx+=2)
		{
			short *p=buf+iw*ky+kx;
			short C0=p[0], C1=p[1], C2=p[iw], C3=p[iw+1];

			C1-=C0;
			C2-=C0;
			C3-=C0;

			//C1-=C2;
			//C2+=C1>>1&mask;
			//C0-=C2;
			//C2+=C0>>1&mask;
			//C3-=C2;
			//C2+=C3>>1&mask;

			p[0   ]=C0;
			p[1   ]=C1;
			p[iw  ]=C2;
			p[iw+1]=C3;
		}
	}
}
void raw_ycmcb_inv(short *buf, int iw, int ih, int idepth)
{
	int mask=((1<<idepth)-1)<<(16-idepth);
	for(int ky=0;ky<ih;ky+=2)
	{
		for(int kx=0;kx<iw;kx+=2)
		{
			short *p=buf+iw*ky+kx;
			short C0=p[0], C1=p[1], C2=p[iw], C3=p[iw+1];

			C1-=C0;
			C2-=C0;
			C3-=C0;

			//C2-=C3>>1&mask;
			//C3+=C2;
			//C2-=C0>>1&mask;
			//C0+=C2;
			//C2-=C1>>1&mask;
			//C1+=C2;

			p[0   ]=C0;
			p[1   ]=C1;
			p[iw  ]=C2;
			p[iw+1]=C3;
		}
	}
}
void colortransform_ycmcb_fwd(short *buf, int iw, int ih, int idepth)//3 channels, stride 4 bytes
{
	int mask=((1<<idepth)-1)<<(16-idepth);
	for(ptrdiff_t k=0, len=(ptrdiff_t)iw*ih*4;k<len;k+=4)
	{
		short r=buf[k], g=buf[k|1], b=buf[k|2];

		r-=g;        //r-g
		g+=r>>1&mask;//g+(r-g)/2 = (r+g)/2
		b-=g;        //b-(r+g)/2
		g+=b>>1&mask;//(r+g)/2+(b-(r+g)/2)/2 = 1/4 r + 1/4 g + 1/2 b

		buf[k  ]=r;//Cm
		buf[k|1]=g;//Y
		buf[k|2]=b;//Cb
	}
}
void colortransform_ycmcb_inv(short *buf, int iw, int ih, int idepth)//3 channels, stride 4 bytes
{
	int mask=((1<<idepth)-1)<<(16-idepth);
	for(ptrdiff_t k=0, len=(ptrdiff_t)iw*ih*4;k<len;k+=4)
	{
		short r=buf[k], g=buf[k|1], b=buf[k|2];//Cm Y Cb
		
		g-=b>>1&mask;
		b+=g;
		g-=r>>1&mask;
		r+=g;

		buf[k  ]=r;
		buf[k|1]=g;
		buf[k|2]=b;
	}
}


//T48: RAW codec

	#define T48_DISABLE_ZIPF
	#define T48_DISABLE_COLORTRANSFORM
//	#define T48_PRINT_ESTIMATOR_CR
//	#define T48_USE_ONE_ESTIMATOR 6
//	#define T48_DISABLE_REC
//	#define T48_DISABLE_COUNTER

#define T48_LR (int)(0.07*0x10000+0.5)
//#define T48_NPRED 15		//just one predictor, for speed

#define T48_REACH 1
#define T48_NNB (T48_REACH*(T48_REACH+1)*4)
//#define T48_NPARAMS (T48_NNB*9+6)		//no pretrained params

#ifndef T48_DISABLE_REC
#define T48_N_REC_ESTIMATORS 6		//15
#define T48_NESTIMATORS (T48_N_REC_ESTIMATORS+1)
//#define T48_NESTIMATORS ((T48_N_REC_ESTIMATORS+1)*T48_NMAPS)
#else
#define T48_NESTIMATORS T48_NMAPS
#endif
typedef struct T48NodeStruct
{
	int n[2];
#ifndef T48_DISABLE_REC
	unsigned short rec[T48_N_REC_ESTIMATORS];
#endif
} T48Node;
typedef struct T48CtxStruct//this codec is for raw (Bayer) images only
{
	int context;
	int idepth;
	ArrayHandle maps[4][16];
	T48Node *node;

	int p0arr[T48_NESTIMATORS], p0_0, p0;//p0_0 isn't clamped
	int weights[4][16][T48_NESTIMATORS];
	long long wsum;

	int nnodes;
#ifdef T48_PRINT_ESTIMATOR_CR
	double csizes_est[T48_NESTIMATORS];
#endif
} T48Ctx;
T48Ctx* t48_ctx_init(int idepth)
{
	int val=0x8000;
	T48Node node0={{1, 1}};
#ifndef T48_DISABLE_REC
	for(int k=0;k<T48_N_REC_ESTIMATORS;++k)
		node0.rec[k]=0x8000;
#endif
	T48Ctx *ctx=(T48Ctx*)malloc(sizeof(*ctx));
	if(!ctx)
	{
		LOG_ERROR("Allocation error");
		return 0;
	}
	memset(ctx, 0, sizeof(*ctx));
	memfill(ctx->weights, &val, sizeof(ctx->weights), sizeof(int));
	ctx->idepth=idepth;
	for(int kc=0;kc<4;++kc)
	{
		for(int kb=idepth-1;kb>=0;--kb)
		{
			ArrayHandle *map=ctx->maps[kc]+kb;
			int nnodes=(1<<idepth)<<(idepth-1-kb);
			ARRAY_ALLOC(T48Node, *map, 0, nnodes, 0, 0);
			memfill(map[0]->data, &node0, map[0]->count*sizeof(node0), sizeof(node0));
			ctx->nnodes+=nnodes;
		}
	}
	return ctx;
}
void t48_ctx_clear(T48Ctx **ctx)
{
	for(int kc=0;kc<4;++kc)
	{
		for(int kb=ctx[0]->idepth-1;kb>=0;--kb)
			array_free(ctx[0]->maps[kc]+kb);
	}
	free(*ctx);
	*ctx=0;
}
#if 0
static int t48_loadnb(const char *pixels, const char *errors, int iw, int ih, int kc, int kx, int ky, short *nb)
{
	int idx=-1;
	for(int ky2=-T48_REACH;ky2<0;++ky2)
	{
		for(int kx2=-T48_REACH;kx2<=T48_REACH;++kx2)
		{
			if((unsigned)(kx+kx2)<(unsigned)iw&&(unsigned)(ky+ky2)<(unsigned)ih)
			{
				int idx2=(iw*(ky+ky2)+kx+kx2)<<2|kc;
				nb[++idx]=pixels[idx2];
				nb[++idx]=errors[idx2];
			}
			else
			{
				nb[++idx]=0;
				nb[++idx]=0;
			}
		}
	}
	for(int kx2=-T48_REACH;kx2<0;++kx2)
	{
		if((unsigned)(kx+kx2)<(unsigned)iw)
		{
			int idx2=(iw*ky+kx+kx2)<<2|kc;
			nb[++idx]=pixels[idx2];
			nb[++idx]=errors[idx2];
		}
		else
		{
			nb[++idx]=0;
			nb[++idx]=0;
		}
	}
	return ++idx;
}
static int t48_dot(const short *a, const short *b, int count)
{
	int k;
	__m256i sum=_mm256_setzero_si256();
	for(k=0;k<count-15;k+=16)//https://stackoverflow.com/questions/62041400/inner-product-of-two-16bit-integer-vectors-with-avx2-in-c
	{
		__m256i va=_mm256_loadu_si256((__m256i*)(a+k));
		__m256i vb=_mm256_loadu_si256((__m256i*)(b+k));
		va=_mm256_madd_epi16(va, vb);
		sum=_mm256_add_epi32(sum, va);
	}
	__m128i s2=_mm_add_epi32(_mm256_extracti128_si256(sum, 1), _mm256_castsi256_si128(sum));
	__m128i hi=_mm_shuffle_epi32(s2, _MM_SHUFFLE(2, 1, 3, 2));
	s2=_mm_add_epi32(s2, hi);
	s2=_mm_hadd_epi32(s2, s2);
	int s3=_mm_extract_epi32(s2, 0);
	for(;k<count;++k)
		s3+=a[k]*b[k];
	return s3;
}
#endif
void t48_ctx_get_context(T48Ctx *ctx, const unsigned short *buf, int iw, int ih, int kx, int ky)
{
	//clamped gradient predictor
#if 0
#define LOAD(X, Y) (unsigned)(kx+(X))<(unsigned)iw&&(unsigned)(ky+(Y))<(unsigned)ih?buf[iw*(ky+(Y))+kx+(X)]-0x8000:0
	short
		N=LOAD(0, -1),
		W=LOAD(-1, 0),
		NW=LOAD(-1, -1);
#undef  LOAD
	short vmin, vmax;
	if(N<W)
		vmin=N, vmax=W;
	else
		vmin=W, vmax=N;
	int pred=N+W-NW;
	pred=CLAMP(vmin, pred, vmax);
	ctx->context=pred;
#endif

	//average predictor
#if 1
#ifdef T48_DISABLE_COLORTRANSFORM
	const int dil=1;
#else
	const int dil=2;
#endif
	long long pred=0, den=0;
	for(int ky2=-T48_REACH;ky2<0;++ky2)
	{
		for(int kx2=-T48_REACH;kx2<=T48_REACH;++kx2)
		{
			if((unsigned)(kx+kx2*dil)<(unsigned)iw&&(unsigned)(ky+ky2*dil)<(unsigned)ih)
			{
				int idx2=iw*(ky+ky2*dil)+kx+kx2*dil;
				int weight=0x10000/(ky2*ky2+kx2*kx2);
				pred+=(long long)(buf[idx2]-0x8000)*weight;
				den+=weight;
			}
		}
	}
	for(int kx2=-T48_REACH;kx2<0;++kx2)
	{
		if((unsigned)(kx+kx2*dil)<(unsigned)iw)
		{
			int idx2=iw*ky+kx+kx2*dil;
			int weight=0x10000/abs(kx2);
			pred+=(long long)(buf[idx2]-0x8000)*weight;
			den+=weight;
		}
	}
	ctx->context=den?(int)(pred/den):0;
#endif
	ctx->context+=0x8000;
	ctx->context>>=16-ctx->idepth;
	ctx->context=CLAMP(0, ctx->context, (1<<ctx->idepth)-1);
}
void t48_ctx_estimate_p0(T48Ctx *ctx, int kc, int kb)
{
	int *wk=ctx->weights[kc][kb];

	int p0idx=0;
	long long sum;
	T48Node *node;

	int k2=0;
	ArrayHandle map=ctx->maps[kc][kb];
	node=ctx->node=(T48Node*)array_at(&map, ctx->context);
		
	sum=node->n[0]+node->n[1];
	ctx->p0arr[p0idx+k2]=sum?(int)(((long long)node->n[0]<<16)/sum):0x8000;
	++k2;
#ifndef T48_DISABLE_REC
	for(;k2<T48_N_REC_ESTIMATORS+1;++k2)
		ctx->p0arr[p0idx+k2]=node->rec[k2-1];
#endif
	p0idx+=k2;


	sum=0;
	ctx->wsum=0;
	for(int k=0;k<T48_NESTIMATORS;++k)
	{
#ifdef T48_DISABLE_COUNTER
		if(k%(T48_N_REC_ESTIMATORS+1))//
#endif
		{
			sum+=(long long)ctx->p0arr[k]*wk[k];
			ctx->wsum+=wk[k];
		}
	}
	ctx->p0=ctx->wsum?(int)(sum/ctx->wsum):0x8000;
	ctx->p0_0=ctx->p0;

	ctx->p0=CLAMP(1, ctx->p0, 0xFFFF);
}
void t48_ctx_update(T48Ctx *ctx, int kc, int kb, int bit)
{
#ifdef T48_PRINT_ESTIMATOR_CR
	for(int k=0;k<T48_NESTIMATORS;++k)
	{
		int prob=bit?0x10000-ctx->p0arr[k]:ctx->p0arr[k];
		prob=CLAMP(1, prob, 0xFFFF);
		//if(prob)
		{
			double p=(double)prob/0x10000;
			double bitsize=-log2(p);
			ctx->csizes_est[k]+=bitsize;
		}
	}
#endif
	//bwd
	int *wk=ctx->weights[kc][kb];
	if(ctx->p0_0>=1&&ctx->p0_0<=0xFFFF)
	{
		int p_bit=bit?0x10000-ctx->p0:ctx->p0;
		long long dL_dp0=-(1LL<<32)/p_bit;//fixed 47.16 bit
		dL_dp0^=-bit;
		dL_dp0+=bit;
		for(int k=0;k<T48_NESTIMATORS;++k)
		{
			int diff=ctx->p0arr[k]-ctx->p0;//fixed 15.16 bit
			long long grad = dL_dp0*diff/ctx->wsum;
			long long wnew=T48_LR*grad>>16;
			wnew=wk[k]-wnew;
			wnew=CLAMP(1, wnew, 0xFFFF);
			wk[k]=(int)wnew;
		}
	}

	//update
	T48Node *node;

	node=ctx->node;
	++node->n[bit];
#ifndef T48_DISABLE_REC
	for(int k=0;k<T48_N_REC_ESTIMATORS;++k)
	{
		int lgden=k;
		//int lgden=(k+1)<<1;
		int temp=node->rec[k]+(((!bit<<16)-node->rec[k])>>lgden);
		node->rec[k]=CLAMP(1, temp, 0xFFFF);
	}
#endif
	ctx->context|=bit<<(ctx->idepth+ctx->idepth-1-kb);
}
int t48_encode(const unsigned short *src, int iw, int ih, int idepth, char *bayer, ArrayHandle *data, int loud)
{
	int res=iw*ih;
	double t_start=time_sec();
	if(loud)
	{
		acme_strftime(g_buf, G_BUF_SIZE, "%Y-%m-%d_%H-%M-%S");
		printf("T48 \'Lossless Raw Image Codec\'  %s  Enc WHD %d*%d*%d = %d bytes\n", g_buf, iw, ih, idepth, iw*ih*idepth/8);
	}
	unsigned short *buf2=(unsigned short*)malloc((size_t)res*sizeof(short));
	T48Ctx *ctx=t48_ctx_init(idepth);
	if(!buf2||!ctx)
	{
		LOG_ERROR("Allocation error");
		return 0;
	}
	if(loud)
		printf("Using %lf MB\n", (double)ctx->nnodes*sizeof(T48Node)/(1024*1024));
	bayer_pack(src, iw, ih, bayer, buf2);
#ifndef T48_DISABLE_COLORTRANSFORM
	bufadd_i16(buf2, iw*ih, 0x8000);
	raw_ycmcb_fwd(buf2, iw, ih, idepth);
	bufadd_i16(buf2, iw*ih, 0x8000);
#endif

	DList list;
	dlist_init(&list, 1, 1024, 0);
	
	ABACEncContext ac;
	abac_enc_init(&ac, &list);
	
#ifndef T48_DISABLE_ZIPF
	double csizes[4][16]={0};
#endif
	
	int w2=iw, h2=ih;
	//int w2=10, h2=10;
	for(int ky=0, idx;ky<h2;++ky)
	{
		for(int kx=0;kx<w2;++kx)
		{
			int kc=(ky&1)<<1|kx&1;
			idx=iw*ky+kx;
			t48_ctx_get_context(ctx, buf2, iw, ih, kx, ky);
			for(int kb=idepth-1;kb>=0;--kb)//MSB -> LSB
			{
				t48_ctx_estimate_p0(ctx, kc, kb);
#ifdef T48_USE_ONE_ESTIMATOR
				unsigned short p0=ctx->p0arr[T48_USE_ONE_ESTIMATOR];
				p0=CLAMP(1, p0, 0xFFFF);
#else
				unsigned short p0=ctx->p0;
#endif

				int bit=buf2[idx]>>(16-idepth+kb)&1;
				abac_enc(&ac, p0, bit);
				
#ifndef T48_DISABLE_ZIPF
				int prob=bit?0x10000-p0:p0;//
				double bitsize=-log2((double)prob/0x10000);
				csizes[kc][kb]+=bitsize;//
#endif

				t48_ctx_update(ctx, kc, kb, bit);
			}
		}
		if(loud)
#ifndef T48_DISABLE_ZIPF
		{
			static double csize_prev=0;
			double csize=0;
			for(int kc=0;kc<4;++kc)
			{
				for(int kb=idepth-1;kb>=0;--kb)
					csize+=csizes[kc][kb]/8;
			}
			printf("%5d/%5d  %6.2lf%%  CR%11f  CR_delta%11f\r", ky+1, ih, 100.*(ky+1)/ih, iw*(ky+1)*idepth/(csize*8), iw*idepth/((csize-csize_prev)*8));
			csize_prev=csize;
		}
#else
			printf("%5d/%5d  %6.2lf%%\r", ky+1, ih, 100.*(ky+1)/ih);
#endif
	}
	abac_enc_flush(&ac);

	size_t dststart=dlist_appendtoarray(&list, data);
	if(loud)
	{
		printf("\n");//skip progress line

		//printf("Used %f MB of memory\n", (float)ctx->nnodes*sizeof(T48Node)/(1024*1024));
		timedelta2str(g_buf, G_BUF_SIZE, time_sec()-t_start);
		printf("Encode elapsed %s\n", g_buf);
		
#ifndef T48_DISABLE_ZIPF
		double chsizes[5]={0};
		int cubitsize=(iw>>1)*(ih>>1);
		printf("\t\tC0\t\tC1\t\tC2\t\tC4\n");
		for(int kb=idepth-1;kb>=0;--kb)
		{
			printf("B%2d  ", kb);
			for(int kc=0;kc<4;++kc)
			{
				double size=csizes[kc][kb];
				printf(" %15.6f", cubitsize/size);
				chsizes[kc]+=size;
			}
			printf("\n");
		}
		printf("\n");
		cubitsize*=idepth;
		chsizes[4]=chsizes[0]+chsizes[1]+chsizes[2]+chsizes[3];
		printf("Total%17.4f %17.4f %17.4f %17.4f %17.6f\n", cubitsize/chsizes[0], cubitsize/chsizes[1], cubitsize/chsizes[2], cubitsize/chsizes[3], (cubitsize<<2)/chsizes[4]);
#endif
		printf("Total size\t%8d    CR %17.6f\n", (int)list.nobj, (float)iw*ih*idepth/(list.nobj<<3));
		//printf("Total size\t%17.6lf\n", csize0/8);
		//double csize1=0;
		//for(int kb=idepth-1;kb>=0;--kb)
		//{
		//	for(int kc=0;kc<4;++kc)
		//		csize1+=csizes[kc][kb];
		//}
		//printf("Total size1\t%17.6lf\n", csize1);

#ifdef T48_PRINT_ESTIMATOR_CR
		printf("Estimator efficiencies:\n");
		for(int ke=0;ke<T48_NESTIMATORS;++ke)
			printf("%2d  %17.6f  %17.6f\n", ke, ctx->csizes_est[ke]/8, iw*ih*idepth/ctx->csizes_est[ke]);
#endif
	}
	t48_ctx_clear(&ctx);
	dlist_clear(&list);
	free(buf2);
	return 1;
}
int t48_decode(const unsigned char *data, size_t srclen, int iw, int ih, int idepth, char *bayer, unsigned short *dst, int loud)
{
	int res=iw*ih;
	double t_start=time_sec();

	unsigned short *buf=(unsigned short*)malloc((size_t)res*sizeof(short));
	T48Ctx *ctx=t48_ctx_init(idepth);
	if(!buf||!ctx)
	{
		LOG_ERROR("Allocation error");
		return 0;
	}

	ABACDecContext ac;
	abac_dec_init(&ac, data, data+srclen);
	
	int w2=iw, h2=ih;
	//int w2=10, h2=10;
	for(int ky=0, idx;ky<h2;++ky)
	{
		for(int kx=0;kx<w2;++kx)
		{
			int kc=(ky&1)<<1|kx&1;
			idx=iw*ky+kx;
			t48_ctx_get_context(ctx, buf, iw, ih, kx, ky);
			for(int kb=idepth-1;kb>=0;--kb)//MSB -> LSB
			{
				t48_ctx_estimate_p0(ctx, kc, kb);
#ifdef T48_USE_ONE_ESTIMATOR
				unsigned short p0=ctx->p0arr[T48_USE_ONE_ESTIMATOR];
				p0=CLAMP(1, p0, 0xFFFF);
#else
				unsigned short p0=ctx->p0;
#endif
					
				int bit=abac_dec(&ac, p0);
				buf[idx]|=bit<<(16-idepth+kb);

				t48_ctx_update(ctx, kc, kb, bit);
			}
		}
		if(loud)
			printf("%5d/%5d  %6.2lf%%\r", ky+1, ih, 100.*(ky+1)/ih);
	}
#ifndef T48_DISABLE_COLORTRANSFORM
	bufadd_i16(buf, iw*ih, 0x8000);
	raw_ycmcb_inv(buf, iw, ih, idepth);
	bufadd_i16(buf, iw*ih, 0x8000);
#endif
	bayer_unpack(buf, iw, ih, bayer, dst);
	if(loud)
	{
		printf("\n");//skip progress line
		timedelta2str(g_buf, G_BUF_SIZE, time_sec()-t_start);
		printf("Decode elapsed %s\n", g_buf);
	}
	t48_ctx_clear(&ctx);
	free(buf);
	return 1;
}
void test48(ImageHandle image, int idepth, char *bayer)
{
	console_start();

	int iw=image->iw, ih=image->ih;
	unsigned short *dst=(unsigned short*)malloc(iw*ih*4*sizeof(short));
	ArrayHandle data=0;
	t48_encode((unsigned short*)image->data, iw, ih, idepth, bayer, &data, 1);
	t48_decode(data->data, data->count, iw, ih, idepth, bayer, dst, 1);
	compare_bufs_u16(dst, (unsigned short*)image->data, iw, ih, 3, 4, "T48", 0);
	free(dst);

	pause();
	console_end();
}


//T49: RAW codec

//	#define T49_DISABLE_ZIPF
//	#define T49_DISABLE_COLORTRANSFORM
//	#define T49_PRINT_ESTIMATOR_CR
//	#define T49_USE_ONE_ESTIMATOR 6
//	#define T49_DISABLE_REC
//	#define T49_DISABLE_COUNTER

#define T49_LR (int)(0.07*0x10000+0.5)
#define T49_NPRED 8

#define T49_REACH 1
#define T49_NNB (T49_REACH*(T49_REACH+1)*4)
//#define T49_NPARAMS (T49_NNB*9+6)		//no pretrained params

#ifndef T49_DISABLE_REC
#define T49_N_REC_ESTIMATORS 6		//15 max
#define T49_NESTIMATORS (T49_N_REC_ESTIMATORS+1)
//#define T49_NESTIMATORS ((T49_N_REC_ESTIMATORS+1)*T49_NMAPS)
#else
#define T49_NESTIMATORS T49_NMAPS
#endif
typedef struct T49NodeStruct
{
#ifndef T49_DISABLE_COUNTER
	int n[2];
#endif
#ifndef T49_DISABLE_REC
	unsigned short rec[T49_N_REC_ESTIMATORS];
#endif
} T49Node;
typedef struct T49CtxStruct
{
	int context[T49_NPRED];
	short idepth, cdepth;
	ArrayHandle maps[4][16];
	T49Node *node[T49_NPRED];

	int p0arr[T49_NPRED*T49_NESTIMATORS], p0_0, p0;//p0_0 isn't clamped
	int weights[4][16][T49_NPRED*T49_NESTIMATORS];
	long long wsum;

	int nnodes;
#ifdef T49_PRINT_ESTIMATOR_CR
	double csizes_est[T49_NESTIMATORS];
#endif
} T49Ctx;
T49Ctx* t49_ctx_init(int nch, int idepth)
{
	int val=0x8000;
	T49Node node0={{1, 1}};
#ifndef T49_DISABLE_REC
	for(int k=0;k<T49_N_REC_ESTIMATORS;++k)
		node0.rec[k]=0x8000;
#endif
	T49Ctx *ctx=(T49Ctx*)malloc(sizeof(*ctx));
	if(!ctx)
	{
		LOG_ERROR("Allocation error");
		return 0;
	}
	memset(ctx, 0, sizeof(*ctx));
	memfill(ctx->weights, &val, sizeof(ctx->weights), sizeof(int));
	ctx->idepth=idepth;
	ctx->cdepth=idepth;
	if(ctx->cdepth>8)
		ctx->cdepth=8;
	for(int kc=0;kc<nch;++kc)
	{
		for(int kb=16-1;kb>16-1-ctx->idepth;--kb)
		{
			ArrayHandle *map=ctx->maps[kc]+kb;
			int nnodes=T49_NPRED<<(ctx->cdepth+16-1-kb);
			ARRAY_ALLOC(T49Node, *map, 0, nnodes, 0, 0);
			memfill(map[0]->data, &node0, map[0]->count*sizeof(node0), sizeof(node0));
			ctx->nnodes+=nnodes;
		}
	}
	return ctx;
}
void t49_ctx_clear(T49Ctx **ctx)
{
	for(int kc=0;kc<4;++kc)
	{
		for(int kb=16-1;kb>16-1-ctx[0]->idepth;--kb)
			array_free(ctx[0]->maps[kc]+kb);
	}
	free(*ctx);
	*ctx=0;
}
void t49_ctx_get_context(T49Ctx *ctx, const unsigned short *buf, int iw, int ih, int kx, int ky, int kc)
{
	long long pred;

	//clamped gradient predictor
#if 1
#define LOAD(X, Y) (unsigned)(kx+(X))<(unsigned)iw&&(unsigned)(ky+(Y))<(unsigned)ih?buf[(iw*(ky+(Y))+kx+(X))<<2|kc]:0
	short
		WW=LOAD(-2,  0),
		NN=LOAD( 0, -2),
		N =LOAD( 0, -1),
		W =LOAD(-1,  0),
		NW=LOAD(-1, -1),
		NE=LOAD( 1, -1);
#undef  LOAD

	int j=-1;

	short vmin, vmax;
	if(N<W)
		vmin=N, vmax=W;
	else
		vmin=W, vmax=N;
	pred=N+W-NW;
	pred=CLAMP(vmin, pred, vmax);
	ctx->context[++j]=(int)pred;

	ctx->context[++j]=N;
	ctx->context[++j]=W;
	ctx->context[++j]=(N+W)/2+(NE-NW)/4;
	ctx->context[++j]=NW;
	pred=(W*2-WW+N*2-NN)>>1, ctx->context[++j]=(int)CLAMP(vmin, pred, vmax);
	pred=W*2-WW, ctx->context[++j]=(int)CLAMP(vmin, pred, vmax);
	pred=N*2-NN, ctx->context[++j]=(int)CLAMP(vmin, pred, vmax);
#endif

	//average predictor
#if 0
#ifdef T49_DISABLE_COLORTRANSFORM
	const int dil=1;
#else
	const int dil=2;
#endif
	long long den=0;
	pred=0;
	for(int ky2=-T49_REACH;ky2<0;++ky2)
	{
		for(int kx2=-T49_REACH;kx2<=T49_REACH;++kx2)
		{
			if((unsigned)(kx+kx2*dil)<(unsigned)iw&&(unsigned)(ky+ky2*dil)<(unsigned)ih)
			{
				int idx2=(iw*(ky+ky2*dil)+kx+kx2*dil)<<2|kc;
				int weight=0x10000/(ky2*ky2+kx2*kx2);
				pred+=(long long)buf[idx2]*weight;
				den+=weight;
			}
		}
	}
	for(int kx2=-T49_REACH;kx2<0;++kx2)
	{
		if((unsigned)(kx+kx2*dil)<(unsigned)iw)
		{
			int idx2=(iw*ky+kx+kx2*dil)<<2|kc;
			int weight=0x10000/abs(kx2);
			pred+=(long long)buf[idx2]*weight;
			den+=weight;
		}
	}
	ctx->context[++j]=den?(int)(pred/den):0;
#endif
	for(int k=0;k<T49_NPRED;++k)
	{
		ctx->context[k]+=0x8000;
		ctx->context[k]=CLAMP(0, ctx->context[k], 0xFFFF);
		ctx->context[k]>>=16-ctx->cdepth;
	}
}
void t49_ctx_estimate_p0(T49Ctx *ctx, int kc, int kb)
{
	int *wk=ctx->weights[kc][kb];

	int p0idx=0;
	long long sum;
	T49Node *node;

	for(int kp=0;kp<T49_NPRED;++kp)
	{
		int k2=0;
		ArrayHandle map=ctx->maps[kc][kb];
		node=ctx->node[kp]=(T49Node*)array_at(&map, ctx->context[kp]);
		
#ifndef T49_DISABLE_COUNTER
		sum=node->n[0]+node->n[1];
		ctx->p0arr[p0idx+k2]=sum?(int)(((long long)node->n[0]<<16)/sum):0x8000;
		++k2;
#endif
#ifndef T49_DISABLE_REC
		for(;k2<T49_N_REC_ESTIMATORS+1;++k2)
			ctx->p0arr[p0idx+k2]=node->rec[k2-1];
#endif
		p0idx+=k2;
	}


	sum=0;
	ctx->wsum=0;
	for(int k=0;k<T49_NPRED*T49_NESTIMATORS;++k)
	{
#ifdef T49_DISABLE_COUNTER
		if(k%(T49_N_REC_ESTIMATORS+1))//
#endif
		{
			sum+=(long long)ctx->p0arr[k]*wk[k];
			ctx->wsum+=wk[k];
		}
	}
	ctx->p0=ctx->wsum?(int)(sum/ctx->wsum):0x8000;
	ctx->p0_0=ctx->p0;

	ctx->p0=CLAMP(1, ctx->p0, 0xFFFF);
}
void t49_ctx_update(T49Ctx *ctx, int kc, int kb, int bit)
{
#ifdef T49_PRINT_ESTIMATOR_CR
	for(int k=0;k<T49_NESTIMATORS;++k)
	{
		int prob=bit?0x10000-ctx->p0arr[k]:ctx->p0arr[k];
		prob=CLAMP(1, prob, 0xFFFF);
		//if(prob)
		{
			double p=(double)prob/0x10000;
			double bitsize=-log2(p);
			ctx->csizes_est[k]+=bitsize;
		}
	}
#endif
	//bwd
	int *wk=ctx->weights[kc][kb];
	if(ctx->p0_0>=1&&ctx->p0_0<=0xFFFF)
	{
		int p_bit=bit?0x10000-ctx->p0:ctx->p0;
		long long dL_dp0=-(1LL<<32)/p_bit;//fixed 47.16 bit
		dL_dp0^=-bit;
		dL_dp0+=bit;
		for(int k=0;k<T49_NPRED*T49_NESTIMATORS;++k)
		{
			int diff=ctx->p0arr[k]-ctx->p0;//fixed 15.16 bit
			long long grad = dL_dp0*diff/ctx->wsum;
			long long wnew=T49_LR*grad>>16;
			wnew=wk[k]-wnew;
			wnew=CLAMP(1, wnew, 0xFFFF);
			wk[k]=(int)wnew;
		}
	}

	//update
	T49Node *node;
	for(int kp=0;kp<T49_NPRED;++kp)
	{
		node=ctx->node[kp];
#ifndef T49_DISABLE_COUNTER
		++node->n[bit];
#endif
#ifndef T49_DISABLE_REC
		for(int k=0;k<T49_N_REC_ESTIMATORS;++k)
		{
			int lgden=k+1;
			//int lgden=k;
			//int lgden=(k+1)<<1;
			int temp=node->rec[k]+(((!bit<<16)-node->rec[k])>>lgden);
			node->rec[k]=CLAMP(1, temp, 0xFFFF);
		}
#endif
		ctx->context[kp]|=bit<<(ctx->cdepth+16-1-kb);
	}
}
int t49_encode(const unsigned short *src, int iw, int ih, int idepth, ArrayHandle *data, int loud)
{
	int res=iw*ih;
	double t_start=time_sec();
	if(loud)
	{
		acme_strftime(g_buf, G_BUF_SIZE, "%Y-%m-%d_%H-%M-%S");
		printf("T49 \'Lossless 16-bit Image Codec\'  %s  Enc  CWHB 3*%d*%d*2 = %d bytes\n", g_buf, iw, ih, 3*iw*ih*2);
	}
	unsigned short *buf2=(unsigned short*)malloc((size_t)res*4*sizeof(short));
	T49Ctx *ctx=t49_ctx_init(3, idepth);
	if(!buf2||!ctx)
	{
		LOG_ERROR("Allocation error");
		return 0;
	}
	if(loud)
		printf("Using %lf MB\n", (double)ctx->nnodes*sizeof(T49Node)/(1024*1024));
	memcpy(buf2, src, (size_t)res*4*sizeof(short));
	bufadd_i16((short*)buf2, iw*ih, 0x8000);//the buffer must be signed
#ifndef T49_DISABLE_COLORTRANSFORM
	colortransform_ycmcb_fwd((short*)buf2, iw, ih, idepth);
#endif

	DList list;
	dlist_init(&list, 1, 1024, 0);
	
	ABACEncContext ac;
	abac_enc_init(&ac, &list);
	
#ifndef T49_DISABLE_ZIPF
	double csizes[4][16]={0};
#endif
	
	int w2=iw, h2=ih;
	//int w2=10, h2=10;
	for(int ky=0, idx;ky<h2;++ky)
	{
		for(int kx=0;kx<w2;++kx)
		{
			for(int kc=0;kc<3;++kc)
			{
				//if(ky==2&&kx==24&&kc==1)//
				//	printf("");

				idx=(iw*ky+kx)<<2|kc;
				int val=(short)buf2[idx]+0x8000;
				t49_ctx_get_context(ctx, buf2, iw, ih, kx, ky, kc);
				for(int kb=16-1;kb>16-1-idepth;--kb)//MSB -> LSB
				{
					//if(ky==1&&kx==22&&kc==2&&kb==7)//
					//	printf("");

					t49_ctx_estimate_p0(ctx, kc, kb);
#ifdef T49_USE_ONE_ESTIMATOR
					unsigned short p0=ctx->p0arr[T49_USE_ONE_ESTIMATOR];
					p0=CLAMP(1, p0, 0xFFFF);
#else
					unsigned short p0=ctx->p0;
#endif

					int bit=val>>kb&1;
					abac_enc(&ac, p0, bit);
				
#ifndef T49_DISABLE_ZIPF
					int prob=bit?0x10000-p0:p0;//
					double bitsize=-log2((double)prob/0x10000);
					csizes[kc][kb]+=bitsize;//
#endif

					t49_ctx_update(ctx, kc, kb, bit);
				}
			}
		}
		if(loud)
#ifndef T49_DISABLE_ZIPF
		{
			static double csize_prev=0;
			double csize=0;
			for(int kc=0;kc<4;++kc)
			{
				for(int kb=16-1;kb>16-1-idepth;--kb)
					csize+=csizes[kc][kb];
			}
			printf("%5d/%5d  %6.2lf%%  CR%11f  CR_delta%11f\r", ky+1, ih, 100.*(ky+1)/ih, iw*(ky+1)*3*idepth/csize, iw*3*idepth/(csize-csize_prev));
			csize_prev=csize;
		}
#else
			printf("%5d/%5d  %6.2lf%%\r", ky+1, ih, 100.*(ky+1)/ih);
#endif
	}
	abac_enc_flush(&ac);

	size_t dststart=dlist_appendtoarray(&list, data);
	if(loud)
	{
		printf("\n");//skip progress line

		//printf("Used %f MB of memory\n", (float)ctx->nnodes*sizeof(T49Node)/(1024*1024));
		timedelta2str(g_buf, G_BUF_SIZE, time_sec()-t_start);
		printf("Encode elapsed %s\n", g_buf);
		
#ifndef T49_DISABLE_ZIPF
		double chsizes[5]={0};
		printf("\t\tC0\t\tC1\t\tC2\n");
		for(int kb=16-1;kb>16-1-idepth;--kb)
		{
			printf("B%2d  ", kb-(16-idepth));
			for(int kc=0;kc<3;++kc)
			{
				double size=csizes[kc][kb];
				printf(" %15.6f", res/size);
				chsizes[kc]+=size;
			}
			printf("\n");
		}
		printf("\n");
		ptrdiff_t ubitsize=(ptrdiff_t)res*idepth;
		chsizes[4]=chsizes[0]+chsizes[1]+chsizes[2]+chsizes[3];
		printf("Total%10.6f %10.6f %10.6f %10.6f\n", ubitsize/chsizes[0], ubitsize/chsizes[1], ubitsize/chsizes[2], (ubitsize*3)/chsizes[4]);
#endif
		printf("Total size\t%8d    CR %17.6lf\n", (int)list.nobj, (double)iw*ih*3*idepth/(list.nobj<<3));
		//printf("Total size\t%17.6lf\n", csize0/8);
		//double csize1=0;
		//for(int kb=idepth-1;kb>=0;--kb)
		//{
		//	for(int kc=0;kc<4;++kc)
		//		csize1+=csizes[kc][kb];
		//}
		//printf("Total size1\t%17.6lf\n", csize1);

#ifdef T49_PRINT_ESTIMATOR_CR
		printf("Estimator efficiencies:\n");
		for(int ke=0;ke<T49_NESTIMATORS;++ke)
			printf("%2d  %17.6f  %17.6f\n", ke, ctx->csizes_est[ke]/8, iw*ih*idepth/ctx->csizes_est[ke]);
#endif
	}
	t49_ctx_clear(&ctx);
	dlist_clear(&list);
	free(buf2);
	return 1;
}
int t49_decode(const unsigned char *data, size_t srclen, int iw, int ih, int idepth, unsigned short *dst, int loud)
{
	int res=iw*ih;
	double t_start=time_sec();

	T49Ctx *ctx=t49_ctx_init(3, idepth);
	if(!ctx)
	{
		LOG_ERROR("Allocation error");
		return 0;
	}
	{
		long long black=0xFFFF000000000000;
		memfill(dst, &black, res*4*sizeof(short), sizeof(black));
	}

	ABACDecContext ac;
	abac_dec_init(&ac, data, data+srclen);
	
	int w2=iw, h2=ih;
	//int w2=10, h2=10;
	for(int ky=0, idx;ky<h2;++ky)
	{
		for(int kx=0;kx<w2;++kx)
		{
			for(int kc=0;kc<3;++kc)
			{
				//if(ky==2&&kx==24&&kc==1)//
				//	printf("");

				idx=(iw*ky+kx)<<2|kc;
				t49_ctx_get_context(ctx, dst, iw, ih, kx, ky, kc);
				for(int kb=16-1;kb>16-1-idepth;--kb)//MSB -> LSB
				{
					//if(ky==1&&kx==22&&kc==2&&kb==7)//
					//	printf("");

					t49_ctx_estimate_p0(ctx, kc, kb);
#ifdef T49_USE_ONE_ESTIMATOR
					unsigned short p0=ctx->p0arr[T49_USE_ONE_ESTIMATOR];
					p0=CLAMP(1, p0, 0xFFFF);
#else
					unsigned short p0=ctx->p0;
#endif
					
					int bit=abac_dec(&ac, p0);
					dst[idx]|=bit<<kb;

					t49_ctx_update(ctx, kc, kb, bit);
				}
				dst[idx]-=0x8000;//unsigned to signed
			}
		}
		if(loud)
			printf("%5d/%5d  %6.2lf%%\r", ky+1, ih, 100.*(ky+1)/ih);
	}
#ifndef T49_DISABLE_COLORTRANSFORM
	colortransform_ycmcb_inv((short*)dst, iw, ih, idepth);
#endif
	bufadd_i16((short*)dst, iw*ih, 0x8000);//the buffer must be signed
	for(int k=0;k<res;++k)
		dst[k<<2|3]=0xFFFF;
	if(loud)
	{
		printf("\n");//skip progress line
		timedelta2str(g_buf, G_BUF_SIZE, time_sec()-t_start);
		printf("Decode elapsed %s\n", g_buf);
	}
	t49_ctx_clear(&ctx);
	return 1;
}
void test49(ImageHandle image, int idepth)
{
	console_start();

	int iw=image->iw, ih=image->ih;
	ArrayHandle data=0;
	unsigned short *dst=(unsigned short*)malloc(iw*ih*4*sizeof(short));
	t49_encode((unsigned short*)image->data, iw, ih, idepth, &data, 1);
	t49_decode(data->data, data->count, iw, ih, idepth, dst, 1);
	compare_bufs_u16(dst, (unsigned short*)image->data, iw, ih, 3, 4, "T49", 0);
	free(dst);

	pause();
	console_end();
}