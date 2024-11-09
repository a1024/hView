#include"util.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<immintrin.h>
static const char file[]=__FILE__;


//	#define SAVE_PNG
//	#define LOUD


#ifdef SAVE_PNG
#include"lodepng.h"
#endif


unsigned short* huf_load(const char *srcfn, int *iw, int *ih, int *nlevels, char *bayermatrix);
int gr_save(const char *dstfn, short *image, int iw, int ih, int nlevels, char *bayermatrix);
short* gr_load(const char *srcfn, int *ret_iw, int *ret_ih, int *ret_nlevels, char *ret_bayermatrix);

int pgm_save_trunc(const char *dstfn, short *image, int iw, int ih, int nlevels);
int pgm_save_clamp(const char *dstfn, short *image, int iw, int ih, int nlevels, int rctdomain);
#ifdef SAVE_PNG
int png16_save(const char *fn, const unsigned short *image, int iw, int ih);
#endif


//.huf
#define console_start()
#define console_log printf
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
unsigned short* huf_load(const char *srcfn, int *iw, int *ih, int *nlevels, char *bayermatrix)
{
	unsigned char *srcbuf=0;
	ptrdiff_t srcsize=0;
	{
		srcsize=get_filesize(srcfn);
		if(srcsize<1)
		{
			LOG_ERROR("Cannot open %s", srcfn);
			return 0;
		}
		FILE *fsrc=fopen(srcfn, "rb");
		if(!fsrc)
		{
			LOG_ERROR("Cannot open %s", srcfn);
			return 0;
		}
		srcbuf=(unsigned char*)malloc(srcsize+16);
		if(!srcbuf)
		{
			fclose(fsrc);
			LOG_ERROR("Alloc error");
			return 0;
		}
		fread(srcbuf, 1, srcsize, fsrc);
		fclose(fsrc);
	}
	HuffHeader *header=(HuffHeader*)srcbuf;
	if(*(int*)header->HUFF!=('H'|'U'<<8|'F'<<16|'F'<<24))
	{
		LOG_ERROR("Unsupported file");
		free(srcbuf);
		return 0;
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
				LOG_ERROR("Invalid Bayer info: %08X = %c%c%c%c",
					*(int*)header->bayerInfo,
					header->bayerInfo[0],
					header->bayerInfo[1],
					header->bayerInfo[2],
					header->bayerInfo[3]
				);
				free(srcbuf);
				return 0;
			}
		}
	}
	else
		memset(bayer_sh2, -1, 4);
	ptrdiff_t res=(ptrdiff_t)header->width*header->height;
	unsigned short *image=0;
	if((header->width|header->height)&1)
	{
		LOG_ERROR("Not a raw photo: %d*%d", header->width, header->height);
		free(srcbuf);
		return 0;
	}
	*iw=header->width>>1;
	*ih=header->height<<1;
	*nlevels=header->nLevels;
	memcpy(bayermatrix, header->bayerInfo, 4);
	if(header->version==1)
	{
		HuffDataHeader *hData=(HuffDataHeader*)(header->histogram+header->nLevels);
		unsigned *bitstream=hData->data;
	//	HuffDecodeCell decroot={0};
		if(*(int*)hData->DATA!=('D'|'A'<<8|'T'<<16|'A'<<24))
		{
			LOG_ERROR("Unsupported file");
			free(srcbuf);
			return 0;
		}
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
				free(srcbuf);
				return 0;
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

			image=(unsigned short*)malloc(res*sizeof(short));
			if(!image)
			{
				LOG_ERROR("Alloc error");
				free(srcbuf);
				return 0;
			}

			//debug decode
#if 1
			int w2=header->width>>1, h2=header->height>>1, res2=w2*h2;
			int bitidx=0, kd=0, kx=0, ky=0;
			while(kd<res&&bitidx<hData->cBitSize)
			{
				int node=rootidx;
				while(bitidx<hData->cBitSize&&(tree[node].branch[0]!=-1||tree[node].branch[1]!=-1))
				{
					int bit=bitstream[bitidx>>5]>>(bitidx&31)&1;
					node=tree[node].branch[bit];
					++bitidx;
				}
				int kc=(ky&1)<<1|(kx&1), kx2=kx>>1, ky2=ky>>1;
				image[res2*kc+w2*ky2+kx2]=tree[node].value;
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
						LOG_ERROR("Unsupported file");
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
					LOG_ERROR("Unsupported file");
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
					LOG_ERROR("Unsupported file");
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
		int *bitstream=(int*)header->histogram;
		int bitidx=0;
		int interleave=*(int*)header->bayerInfo!=0&&*(int*)header->bayerInfo!=1;
		int w2=header->width>>1, h2=header->height>>1, res2=w2*h2;
		image=(unsigned short*)malloc(res*sizeof(short));
		if(!image)
		{
			LOG_ERROR("Alloc error");
			free(srcbuf);
			return 0;
		}
		for(int ky=0;ky<(int)header->height;++ky)
		{
			int prev=0;

			int iy2=ky>=h2;
			int ky0=ky-(h2&-iy2);	//ky0 = ky>=h/2 ? (ky-h/2)*2+1 : ky*2	= {0, 2, 4, ...h&~1, 1, 3, 5, ...h}
			ky0=ky0<<1|iy2;
			if(!interleave)ky0=ky;

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
				
				int kc=(ky0&1)<<1|(kx0&1), kx2=kx0>>1, ky2=ky0>>1;
				image[res2*kc+w2*ky2+kx2]=prev;
			}
		}
	}
	else
	{
		LOG_ERROR("Unsupported file");
		free(srcbuf);
		return 0;
	}
	free(srcbuf);
	return image;
}

int gr_save(const char *dstfn, short *image, int iw, int ih, int nlevels, char *bayermatrix)
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
short* gr_load(const char *srcfn, int *ret_iw, int *ret_ih, int *ret_nlevels, char *ret_bayermatrix)
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

int pgm_save_trunc(const char *dstfn, short *image, int iw, int ih, int nlevels)
{
	ptrdiff_t res=(ptrdiff_t)iw*ih;
	unsigned char *dstbuf=(unsigned char*)malloc(res);
	if(!dstbuf)
	{
		LOG_ERROR("Alloc error");
		return 1;
	}
	char sh=FLOOR_LOG2(nlevels)-8;
	for(ptrdiff_t k=0;k<res;++k)
	//	dstbuf[k]=image[k]&255;
		dstbuf[k]=image[k]>>sh;
	{
		FILE *fdst=fopen(dstfn, "wb");
		if(!fdst)
		{
			LOG_ERROR("Cannot open \"%s\"", dstfn);
			return 1;
		}
		fprintf(fdst, "P5\n%d %d\n255\n", iw, ih);
		fwrite(dstbuf, 1, res, fdst);
		fclose(fdst);
	}
	free(dstbuf);
	return 0;
}
int pgm_save_clamp(const char *dstfn, short *image, int iw, int ih, int nlevels, int yidx)
{
	ptrdiff_t res=(ptrdiff_t)iw*ih;
	unsigned char *dstbuf=(unsigned char*)malloc(res);
	if(!dstbuf)
	{
		LOG_ERROR("Alloc error");
		return 1;
	}
	for(ptrdiff_t k=0;k<res;++k)
	{
		int val=image[k];
		if((size_t)(k-(res>>2)*yidx)>=(size_t)(res>>2))
		{
			val-=nlevels>>1;
			val+=128;
		}
		CLAMP2(val, 0, 255);
		dstbuf[k]=val;
	}
	{
		FILE *fdst=fopen(dstfn, "wb");
		if(!fdst)
		{
			LOG_ERROR("Cannot open \"%s\"", dstfn);
			return 1;
		}
		fprintf(fdst, "P5\n%d %d\n255\n", iw, ih);
		fwrite(dstbuf, 1, res, fdst);
		fclose(fdst);
	}
	free(dstbuf);
	return 0;
}

#ifdef SAVE_PNG
int png16_save(const char *fn, const unsigned short *image, int iw, int ih)
{
	ptrdiff_t imsize=(ptrdiff_t)iw*ih;
	unsigned short *bigimage=(unsigned short*)malloc(imsize*sizeof(short));
	if(!bigimage)
	{
		LOG_ERROR("Alloc error");
		return 0;
	}
	for(int k=0;k<imsize;++k)
		bigimage[k]=image[k]<<8|image[k]>>8;
	int error=lodepng_encode_file(fn, (unsigned char*)bigimage, iw, ih, LCT_GREY, 16);
	free(bigimage);
	return error;
}
#endif

static int rct_planar_raw(unsigned short *image, int iw, int ih, int nlevels, char *bayermatrix, int fwd)
{
	int h4=ih>>2, res=iw*h4, half=nlevels>>1;
	char rggb[4]={-1, -1, -1, -1};
	for(int kc=0, kg=0;kc<4;++kc)
	{
		switch(bayermatrix[kc])
		{
		case 'R':case 'r':rggb[0]=kc;break;
		case 'G':case 'g':rggb[kg++ + 1]=kc;break;
		case 'B':case 'b':rggb[3]=kc;break;
		}
	}
	short *comp[]=
	{
		(short*)image+res*rggb[0],
		(short*)image+res*rggb[1],
		(short*)image+res*rggb[2],
		(short*)image+res*rggb[3],
	};
	if(fwd)
	{
		for(int k=0;k<res;++k)
		{
			int r, g1, g2, b;
			
			r	=*comp[0]; g1	=*comp[1];
			g2	=*comp[2]; b	=*comp[3];

			b-=(g1+g2-half*2)>>1;
			g1-=r-half;
			g2-=r-half;
			r+=(g1+g2-half*2)>>2;

			*comp[0]++=r;
			*comp[1]++=g1;
			*comp[2]++=g2;
			*comp[3]++=b;
		}
	}
	else
	{
		for(int k=0;k<res;++k)
		{
			int r, g1, g2, b;
			
			r	=*comp[0]; g1	=*comp[1];
			g2	=*comp[2]; b	=*comp[3];
			
			r-=(g1+g2-half*2)>>2;
			g2+=r-half;
			g1+=r-half;
			b+=(g1+g2-half*2)>>1;

			*comp[0]++=r;
			*comp[1]++=g1;
			*comp[2]++=g2;
			*comp[3]++=b;
		}
	}
	return rggb[0];
}

int main(int argc, char **argv)
{
#ifdef _DEBUG
//#if 1
	const char *srcfn=
		"E:/Share Box/Scope/20241107/20241107_164228_958.huf"	//7408496/7990272 bytes
		;
	const char *dstfn=
		"D:/ML/mystery.gr"	//3970607/7990272 bytes
		;
#else
	if(argc!=3)
	{
		printf("Usage:  %s  in.huf  out.gr\n", argv[0]);
		return 0;
	}
	const char *srcfn=argv[1], *dstfn=argv[2];
#endif

	unsigned short *image=0;
	int iw=0, ih=0, nlevels=0;
	char bayermatrix[4]={0};
	
	double t=time_sec();
	image=huf_load(srcfn, &iw, &ih, &nlevels, bayermatrix);
	t=time_sec()-t;
#ifdef LOUD
	int usize=(iw*ih*10+7)>>3;
	printf("HUF.D %10lf sec %12.6lf MB/s\n", t, usize/(t*1024*1024));
#endif

#ifdef SAVE_PNG
//	png16_save("20241109_181300_huf_0.png", image, iw, ih);
//	pgm_save_clamp("20241109_181300_huf_0.pgm", (short*)image, iw, ih, nlevels, -1);
//	int yidx=rct_planar_raw(image, iw, ih, nlevels, bayermatrix, 1);
//	png16_save("20241109_181300_huf_rct.png", image, iw, ih);
//	pgm_save_clamp("20241109_181300_huf_rct.pgm", (short*)image, iw, ih, nlevels, yidx);
#endif

	t=time_sec();
	gr_save(dstfn, (short*)image, iw, ih, nlevels, bayermatrix);
	t=time_sec()-t;
#ifdef LOUD
	printf("GR .E %10lf sec %12.6lf MB/s\n", t, usize/(t*1024*1024));
#endif

#if 0
	{
		unsigned short *im2=0;
		int iw2=0, ih2=0, nlevels2=0;
		char bayer2[4]={0};
		
		t=time_sec();
		im2=gr_load(dstfn, &iw2, &ih2, &nlevels2, bayer2);
		t=time_sec()-t;
#ifdef LOUD
		printf("GR .D %10lf sec %12.6lf MB/s\n", t, usize/(t*1024*1024));
#endif

		int res=iw*ih;
		for(int k=0;k<res;++k)
		{
			if(im2[k]!=image[k])
			{
				LOG_ERROR("error");
				printf("");
			}
		}
		free(im2);
	}
#endif
	free(image);
	
	(void)rct_planar_raw;
	(void)huf_freetable;
	(void)huf_buildtable;
	(void)huf_tree_debugcheck;
	(void)huf_tree_debugprint;

	return 0;
}
