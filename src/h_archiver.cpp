#include		"generic.h"
#include		"hview.h"
#include		"huff.h"
#include		"vector_bool.h"
#include		<vector>
#include		<queue>
#include		<stack>
#include		<assert.h>
const char		file[]=__FILE__;

//#define		DEBUG_VEC_BOOL
#define			PRINT_ALPHABET
#define			PRINT_HISTOGRAM
//#define		DEBUG_ARCH

static void		print_bin(const byte *data, int bytesize)
{
	for(int kb=0;kb<(bytesize<<3);++kb)
	{
		printf("%d", data[kb>>3]>>(kb&7)&1);
		if((kb&7)==7)
			printf("-");
	}
}
void			print_histogram(int *histogram, int nlevels, int scanned_size)
{
	int histmax=0;
	for(int k=0;k<nlevels;++k)
		if(histmax<histogram[k])
			histmax=histogram[k];
	const int consolechars=79-15;
	if(!histmax)
		return;
	printf("symbol, freq, %%\n");
	for(int k=0;k<nlevels;++k)
	{
		if(!histogram[k])
			continue;
		printf("%4d %6d %2d ", k, histogram[k], 100*histogram[k]/scanned_size);
		for(int kr=0, count=histogram[k]*consolechars/histmax;kr<count;++kr)
			printf("*");
		printf("\n");
	}
	printf("\n");
}



struct			AlphabetComparator
{
	vector_bool const *alphabet;
	AlphabetComparator(vector_bool const *alphabet):alphabet(alphabet){}
	bool operator()(int idx1, int idx2)const
	{
		auto &s1=alphabet[idx1], &s2=alphabet[idx2];
		for(int kb=0;kb<s1.bitSize&&kb<s2.bitSize;++kb)
		{
			int bit1=s1.get(kb), bit2=s2.get(kb);
			if(bit1!=bit2)
				return bit1<bit2;
		}
		return s1.bitSize<s2.bitSize;//shortest symbol first
		//return s1.bitSize>s2.bitSize;//longest symbol first
	}
};
void			sort_alphabet(vector_bool const *alphabet, int nLevels, std::vector<int> &idx)
{
	idx.resize(nLevels);
	for(int k=0;k<nLevels;++k)
		idx[k]=k;
	std::sort(idx.begin(), idx.end(), AlphabetComparator(alphabet));
}
/*struct		Code
{
	vector_bool code;
	int symbol;
};
bool			alphabet_compare_less(vector_bool const &a, vector_bool const &b)
{
}//*/
void			print_alphabet(vector_bool const *alphabet, const int *histogram, int nlevels, int symbols_to_compress, const int *sort_idx)
{
	printf("symbol");
	if(histogram)
		printf(", freq, %%");
	printf("\n");
	for(int k=0;k<nlevels;++k)//print alphabet
	{
		int symbol=sort_idx?sort_idx[k]:k;
		if(histogram&&!histogram[symbol])
			continue;
		printf("%4d ", symbol);
		if(histogram)
		{
			printf("%6d ", histogram[symbol]);
			printf("%2d ", histogram[symbol]*100/symbols_to_compress);
		}
		auto &code=alphabet[symbol];
		for(int k2=0, k2End=code.bitSize;k2<k2End;++k2)
			printf("%c", char('0'+code.get(k2)));
		printf("\n");
	}
}

struct			Node
{
	Node *branch[2];
	unsigned short value;
	int count;
};
Node*			make_node(int symbol, int count, Node *left, Node *right)//https://gist.github.com/pwxcoo/72d7d3c5c3698371c21e486722f9b34b
{
	Node *n=new Node();
	n->value=symbol, n->count=count;
	n->branch[0]=left, n->branch[1]=right;
	return n;
}
Node*			build_tree(int *histogram, int nLevels)
{
	auto cmp=[](Node* const &a, Node* const &b){return a->count>b->count;};
	std::priority_queue<Node*, std::vector<Node*>, decltype(cmp)> pq(cmp);
	for(int k=0;k<nLevels;++k)
		pq.push(make_node(k, histogram[k], nullptr, nullptr));
	while(pq.size()>1)//build Huffman tree
	{
		Node *left=pq.top();	pq.pop();
		Node *right=pq.top();	pq.pop();
		pq.push(make_node(0, left->count+right->count, left, right));
	}
	return pq.top();
}
void			free_tree(Node *root)
{
	if(root->branch[0])
		free_tree(root->branch[0]);
	if(root->branch[1])
		free_tree(root->branch[1]);
	free(root);
}
void			make_alphabet(Node *root, vector_bool *alphabet)
{
	typedef std::pair<Node*, vector_bool> TraverseInfo;
	std::stack<TraverseInfo> s;
	s.push(TraverseInfo(root, vector_bool()));
	vector_bool left, right;
	while(s.size())//depth-first
	{
		auto &info=s.top();
		Node *r2=info.first;
		if(!r2)
		{
			s.pop();
			continue;
		}
		if(!r2->branch[0]&&!r2->branch[1])
		{
			alphabet[r2->value]=std::move(info.second);
			s.pop();
			continue;
		}
		left=std::move(info.second);
		right=left;
		s.pop();
		if(r2->branch[1])
		{
			right.push_back(true);
			s.push(TraverseInfo(r2->branch[1], std::move(right)));
		}
		if(r2->branch[0])
		{
			left.push_back(false);
			s.push(TraverseInfo(r2->branch[0], std::move(left)));
		}
	}
}
void			calculate_histogram(const short *image, int size, int *histogram, int nLevels)
{
	memset(histogram, 0, nLevels*sizeof(int));
	for(int k=0;k<size;++k)
	{
		//if(image[k]>=nLevels)
		//	LOGE("Image [%d] = %d", k, image[k]);
		++histogram[image[k]];
	}
}
int				compress_huff(const short *buffer, int bw, int bh, int depth, int bayer, std::vector<int> &data)
{
	short *temp=nullptr;
	const short *b2;
	int width, height, imSize;
	if(bayer)//raw color
		width=bw, height=bh, imSize=width*height, b2=buffer;
	else//grayscale
	{
		width=bw>>1, height=bh>>1, imSize=width*height;
		depth+=2;
		temp=new short[imSize];
		for(int ky=0;ky<height;++ky)
		{
			int ky2=ky<<1;
			const short *row=buffer+bw*ky2, *row2=buffer+bw*(ky2+1);
			for(int kx=0;kx<width;++kx)
			{
				int kx2=kx<<1;
				temp[width*ky+kx]=row[kx2]+row[kx2+1]+row2[kx2]+row2[kx2+1];
			}
		}
		b2=temp;
	}
	int nLevels=1<<depth;

	data.resize(sizeof(HuffHeader)/sizeof(int)+nLevels);
	auto header=(HuffHeader*)data.data();
	*(int*)header->HUFF='H'|'U'<<8|'F'<<16|'F'<<24;
	//header->HUFF[0]='H';
	//header->HUFF[1]='U';
	//header->HUFF[2]='F';
	//header->HUFF[3]='F';
	header->version=1;
	header->width=width;
	header->height=height;
	*(int*)header->bayerInfo=bayer;
	header->nLevels=nLevels;

	int *histogram=(int*)((HuffHeader*)data.data())->histogram;
	calculate_histogram(b2, imSize, histogram, nLevels);


	Node *root=build_tree(histogram, nLevels);

	std::vector<vector_bool> alphabet(nLevels);
	make_alphabet(root, alphabet.data());
	//auto alphabet=new vector_bool[nLevels];
	//make_alphabet(root, alphabet);
#ifdef PRINT_ALPHABET
	std::vector<int> indices;
	sort_alphabet(alphabet.data(), nLevels, indices);
	//std::sort(alphabet.begin(), alphabet.end(), AlphabetComparator(alphabet.data()));
	printf("Sorted alphabet\n");
	print_alphabet(alphabet.data(), histogram, nLevels, imSize, indices.data());//
#endif

	free_tree(root);
	
#ifdef DEBUG_VEC_BOOL
	printf("Codes:\n");
	//for(int k=0;k<70;++k)
	//{
	//	printf("%d", k&7);
	//	if((k&7)==7)
	//		printf(" ");
	//}
	//printf("\n");
/*	for(int k=0, col=0;k<imSize;++k)
	{
		if(col+alphabet[b2[k]].bitSize+1>=80)
		{
			printf("\n");
			col=0;
		}
		alphabet[b2[k]].debug_print();
		printf(" ");
		col+=alphabet[b2[k]].bitSize+1;
	}
	printf("\n");//*/
	for(int ky=0, k=0;ky<bh;++ky)
	{
		for(int kx=0;kx<bw;++kx)
		{
			auto &code=alphabet[b2[bw*ky+kx]];
			code.debug_print(k);
			k+=code.bitSize;
			printf(" ");
		}
		printf("\n");
	}
	printf("\n");
#endif
	vector_bool bits;
	for(int k=0;k<imSize;++k)
		bits.push_back(alphabet[b2[k]]);
	bits.clear_tail();
#ifdef DEBUG_VEC_BOOL
	printf("\nConcatenated bits:\n");
	bits.debug_print(0);
	printf("\n");
#endif
	int data_start=data.size();
	data.resize(data_start+sizeof(HuffDataHeader)+bits.size_bytes()/sizeof(int));
	auto dataHeader=(HuffDataHeader*)(data.data()+data_start);
	*(int*)dataHeader->DATA='D'|'A'<<8|'T'<<16|'A'<<24;
	//dataHeader->DATA[0]='D';
	//dataHeader->DATA[1]='A';
	//dataHeader->DATA[2]='T';
	//dataHeader->DATA[3]='A';
	dataHeader->uPxCount=imSize;
	dataHeader->cBitSize=bits.bitSize;
	memcpy(dataHeader->data, bits.data.data(), bits.size_bytes());
	//memcpy(dataHeader->data, bits.data, bits.size_bytes());

	//delete[] alphabet;
	if(!bayer)
	{
		delete[] temp;
		b2=buffer;
	}
	return data_start;
}


//struct		Symbol
//{
//	vector_bool code;
//	int val;
//};
//bool			operator<(Symbol const &a, Symbol const &b){return a.code<b.code;}
//struct			DecNode;
//struct			Choice
//{
//	int val;
//	int nbits;
//	DecNode *next;
//};
//struct			DecNode
//{
//	DecNode *branch[16];
//	unsigned short value;
//	int count;
//	//Choice choices[16];
//};
//DecNode*		make_dec_node(int symbol, int count, Node **choices)//https://gist.github.com/pwxcoo/72d7d3c5c3698371c21e486722f9b34b
//{
//	auto n=new DecNode();
//	n->value=symbol, n->count=count;
//	memcpy(n->branch, choices, 16*sizeof(DecNode*));
//	return n;
//}
//DecNode*		build_dec_tree(int *histogram, int nLevels)
//{
//	auto cmp=[](DecNode* const &a, DecNode* const &b){return a->count>b->count;};
//	std::priority_queue<DecNode*, std::vector<DecNode*>, decltype(cmp)> pq(cmp);
//	for(int k=0;k<nLevels;++k)
//		pq.push(make_node(k, histogram[k], nullptr, nullptr));
//	while(pq.size()>1)//build Huffman tree
//	{
//		Node *left=pq.top();	pq.pop();
//		Node *right=pq.top();	pq.pop();
//		pq.push(make_node(0, left->count+right->count, left, right));
//	}
//	return pq.top();
//}
struct			DecNode
{
	int symbol, bitlen;
	Node *next;
};
const int		lookup_bits=8,
				lookup_mask=(1<<lookup_bits)-1;
DecNode			dec_root[1<<lookup_bits];//depends on the naive 1bit tree
inline int		reverse8bits(int x)
{
	//int x2=0;
	//while(x)
	//{
	//	x2=x2<<1|x&1;
	//	x>>=1;
	//}
	//return x2;

	return x;
}
void			build_dec_tree(Node *root)
{
	memset(dec_root, 0, sizeof(dec_root));
	auto dst=new DecNode;
	typedef std::pair<Node*, vector_bool> TraverseInfo;
	std::stack<TraverseInfo> s;
	s.push(TraverseInfo(root, vector_bool()));
	vector_bool left, right;
	while(s.size())//depth-first
	{
		auto &info=s.top();
		Node *r2=info.first;
		if(!r2)
		{
			s.pop();
			continue;
		}
		int rev_idx;
		if(info.second.bitSize==8)//long code
		{
			rev_idx=reverse8bits(info.second.data[0]);
			auto &node2=dec_root[rev_idx];
#ifdef DEBUG_ARCH
			if(node2.next||node2.symbol)
				printf("overwrite error: idx=0x%02X, r(idx)=0x%02X, symbol [%p]/%d->[%p]\n", info.second.data[0], rev_idx, node2.next, node2.symbol, r2);
#endif
			node2.next=r2;
			s.pop();
			continue;
		}
		if(!r2->branch[0]&&!r2->branch[1])//short code
		{
			int lsb_count=lookup_bits-info.second.bitSize;
			int symbol=info.second.data[0];
			int sh_code=symbol<<lsb_count;
			for(int k=0, count=1<<lsb_count;k<count;++k)
			{
				rev_idx=reverse8bits(sh_code+k);
				auto &node=dec_root[rev_idx];//MSB-first -> LSB-first
			//	auto &node=dec_root[sh_code+k];
#ifdef DEBUG_ARCH
				if(node.next||node.symbol)
					printf("overwrite error: idx=0x%02X, r(idx)=0x%02X, symbol [%p]/%d->%d\n", sh_code+k, rev_idx, node.next, node.symbol, symbol);
#endif
				node.symbol=symbol;
				node.bitlen=info.second.bitSize;
				node.next=nullptr;
			}
			s.pop();
			continue;
		}
		//left.data=nullptr;//
		left=std::move(info.second);
		right=left;
		s.pop();
		if(r2->branch[1])
		{
			right.push_back(true);
			s.push(TraverseInfo(r2->branch[1], std::move(right)));
		}
		if(r2->branch[0])
		{
			left.push_back(false);
			s.push(TraverseInfo(r2->branch[0], std::move(left)));
		}
	}
}
bool			decompress_huff(const byte *data, int bytesize, RequestedFormat format, void **pbuffer, int &bw, int &bh, int &depth, char *bayer_sh)//realloc will be used on buffer
{
#ifdef DEBUG_ARCH
	console_start_good();
	//console_start();
	printf("decode_huff\n");
#endif
	auto header=(HuffHeader*)data;
	if(*(int*)header->HUFF!=('H'|'U'<<8|'F'<<16|'F'<<24)||header->nLevels>(1<<16))
	{
		LOG_ERROR("Invalid file tag: %c%c%c%c, ver: %d, w=%d, h=%d", header->HUFF[0], header->HUFF[1], header->HUFF[2], header->HUFF[3], header->version, header->width, header->height);
#ifdef DEBUG_ARCH
		console_pause();
		console_end();
#endif
		return false;
	}
	auto hData=(HuffDataHeader*)(header->histogram+header->nLevels);
	if(*(int*)hData->DATA!=('D'|'A'<<8|'T'<<16|'A'<<24))
	{
		LOG_ERROR("Invalid data tag: %c%c%c%c, pxCount=%d, bitSize=%lld", hData->DATA[0], hData->DATA[1], hData->DATA[2], hData->DATA[3], hData->uPxCount, hData->cBitSize);
#ifdef DEBUG_ARCH
		console_pause();
		console_end();
#endif
		return false;
	}
	
	char bayer_sh2[4]={};
	if(header->bayerInfo)
	{
		char *bayerInfo=(char*)&header->bayerInfo;
		for(int k=0;k<4;++k)
		{
			switch(bayerInfo[k])
			{
			case 'R':bayer_sh2[k]=16;break;
			case 'G':bayer_sh2[k]=8;break;
			case 'B':bayer_sh2[k]=0;break;
			default:
				LOG_ERROR("Invalid Bayer info: %c%c%c%c", bayerInfo[0], bayerInfo[1], bayerInfo[2], bayerInfo[3]);
#ifdef DEBUG_ARCH
				console_pause();
				console_end();
#endif
				return false;
			}
		}
	}
#ifdef PRINT_HISTOGRAM
	console_start_good();
	print_histogram((int*)header->histogram, header->nLevels, hData->uPxCount);
#endif
	int imSize=header->width*header->height;
	auto src=hData->data;
	short *dst=new short[imSize];

	auto root=build_tree((int*)header->histogram, header->nLevels);
	
#ifdef PRINT_ALPHABET
	console_start_good();
	//console_start();
	std::vector<vector_bool> alphabet(header->nLevels);
	make_alphabet(root, alphabet.data());
	std::vector<int> indices;
	sort_alphabet(alphabet.data(), header->nLevels, indices);
	printf("Sorted alphabet\n");
	print_alphabet(alphabet.data(), (int*)header->histogram, header->nLevels, imSize, indices.data());//

	print_bin((byte*)src, (data+bytesize)-(byte*)src);
	printf("\n");
#endif

/*	build_dec_tree(root);
	int bit_idx=0, kd=0;
	for(;kd<imSize&&bit_idx<hData->cBitSize;++kd)//read bits, LSB-first
	{
		int ex_idx=bit_idx>>5, in_idx=bit_idx&31;
		int lookup;
		if(in_idx)
			lookup=src[ex_idx+1]<<(31-in_idx)|src[ex_idx]>>in_idx;//oldest on right
		else
			lookup=src[ex_idx];

		auto &node=dec_root[lookup&0xFF];
		if(node.next)//long code
		{
			bit_idx+=8;
			Node *prev=nullptr;
			for(auto n2=node.next;n2&&bit_idx<hData->cBitSize;++bit_idx)
			{
				char bit=src[ex_idx]>>in_idx&1;
				prev=n2;
				n2=n2->branch[bit];
			}
			--bit_idx;
			if(prev)
				dst[kd]=prev->value;
#ifdef DEBUG_ARCH
			else
				printf("error: unreachable\n");
#endif
		}
		else//short code
		{
			dst[kd]=node.symbol;
			bit_idx+=node.bitlen;
		}
	}//*/

	int bit_idx=0, kd=0;//naive 1bit decode
	for(;kd<imSize&&bit_idx<hData->cBitSize;++kd)
	{
		auto prev=root, node=root;
		while(bit_idx<hData->cBitSize&&(node->branch[0]||node->branch[1]))
		//while(bit_idx<hData->cBitSize&&!node->value)//what about symbol=0?
		{
			int ex_idx=bit_idx>>5, in_idx=bit_idx&31;
			int bit=src[ex_idx]>>in_idx&1;
			prev=node;
			node=node->branch[bit];
			++bit_idx;
		}
		dst[kd]=node->value;
		//Node *node=nullptr;
		//for(auto n2=root;n2&&bit_idx<hData->cBitSize;++bit_idx)
		//{
		//	int ex_idx=bit_idx>>5, in_idx=bit_idx&31;
		//	int bit=src[ex_idx]>>in_idx&1;
		//	node=n2;
		//	n2=n2->branch[bit];
		//}
		//--bit_idx;
		//if(node)
		//	dst[kd]=node->value;
//#ifdef DEBUG_ARCH
//		else
//			printf("error: unreachable\n");
//#endif
	}
	if(bit_idx!=hData->cBitSize)
	{
		console_start_good();
		//console_start();
		printf("Decompression error:\n");
		printf("\tbit_idx = %d\n", bit_idx);
		printf("\tbitsize = %lld\n", hData->cBitSize);
		printf("\n");
		//printf("Decompression error: bit_idx=%d != bitsize=%lld\n", bit_idx, hData->cBitSize);
		console_pause();
	}

	free_tree(root);
	
	//on success
	void *b2=nullptr;
	switch(format)
	{
	case RF_I8_RGBA:
		b2=realloc(*pbuffer, imSize<<2);
		if(b2)
			*pbuffer=b2;
		break;
	case RF_I16_BAYER:
		b2=realloc(*pbuffer, imSize<<1);
		if(b2)
			*pbuffer=b2;
		memcpy(*pbuffer, dst, imSize<<1);
		break;
	case RF_F32_BAYER:
		{
			float normal=1.f/header->nLevels;
			b2=realloc(*pbuffer, imSize<<2);
			if(b2)
				*pbuffer=b2;
			auto fbuf=(float*)*pbuffer;
			for(int k=0;k<imSize;++k)
				fbuf[k]=dst[k]*normal;
		}
		break;
	}
	delete[] dst;
	for(int k=0;k<4;++k)
		bayer_sh[k]=bayer_sh2[k];
	bw=header->width, bh=header->height, depth=floor_log2(header->nLevels);
#ifdef DEBUG_ARCH
	console_pause();
	console_end();
#endif
	return true;
}


//test functions
/*void			make_alphabet(Node *root, std::vector<bool> *alphabet)//depth-first
{
	typedef std::pair<Node*, std::vector<bool>> TraverseInfo;
	std::stack<TraverseInfo> s;
	s.push(TraverseInfo(root, std::vector<bool>()));
	while(s.size())
	{
		auto &info=s.top();
		Node *r2=info.first;
		if(!r2)
		{
			s.pop();
			continue;
		}
		if(!r2->branch[0]&&!r2->branch[1])
			alphabet[r2->value]=std::move(info.second);
		std::vector<bool> left=std::move(info.second),
				right=left;
		s.pop();
		left.push_back(0), right.push_back(1);
		if(r2->branch[0])
			s.push(TraverseInfo(r2->branch[0], std::move(left)));
		if(r2->branch[1])
			s.push(TraverseInfo(r2->branch[1], std::move(right)));
	}
}
void			print_alphabet(std::vector<bool> *alphabet, int *histogram, int nlevels, int symbols_to_compress)
{
	printf("symbol");
	if(histogram)
		printf(", freq, %%");
	printf("\n");
	for(int k=0;k<nlevels;++k)//print alphabet
	{
		printf("%4d ", k);
		if(histogram)
		{
			printf("%6d ", histogram[k]);
			printf("%2d ", histogram[k]*100/symbols_to_compress);
		}
		auto &symbol=alphabet[k];
		for(int k2=0, k2End=symbol.size();k2<k2End;++k2)
			printf("%c", char('0'+symbol[k2]));
		printf("\n");
	}
}//*/
int				hamming_weight(int x)
{
	x=x-((x>>1)&0x55555555);
	x=(x&0x33333333)+((x>>2)&0x33333333);
	return ((x+(x>>4)&0xF0F0F0F)*0x1010101)>>24;
}
static void		print_subimage(const short *buffer, int bw, int bh, int x0, int y0, int dx, int dy, int digits)
{
	for(int ky=0;ky<dy;++ky)
	{
		for(int kx=0;kx<dy;++kx)
			printf(" %*d", digits, (int)buffer[bw*(y0+ky)+x0+kx]);
		printf("\n");
	}
	printf("\n");
}
static bool		compare_images(short *src, short *dst, int bw, int bh, int maxerrors)
{
	bool identical=true;
	for(int ky=0, kp=0;ky<bh;++ky)
	{
		for(int kx=0;kx<bw;++kx)
		{
			int p1=src[bw*ky+kx], p2=dst[bw*ky+kx];
			if(p1!=p2)//error
			{
				identical=false;
				printf("(%d, %d) src=%d, dst=%d\n", kx, ky, p1, p2);
				++kp;
				if(kp>maxerrors)
				{
					printf("Errors exceeded %d, stopping.\n", maxerrors);
					goto compare_images_exit;
				}
			}
		}
	}
compare_images_exit:
	return identical;
	//if(identical)
	//	printf("Buffers are identical\n");
}
void			archiver_test()
{
	console_start_good();
	//console_start();
	//console_buffer_size(80, 9000);
	//system("MODE CON COLS=80 LINES=4000");

	int bw=3264, bh=2448, depth=10, nLevels_printed=128;
	//int bw=16, bh=16, depth=6, nLevels_printed=1<<(depth-1);
	int imsize=bw*bh, nLevels=1<<depth, bayer='G'|'R'<<8|'B'<<16|'G'<<24;

	//bw=8, bh=8, imsize=bw*bh;
	//short src[]=
	//{
	//	21, 25,  4,  7, 17, 23,  5, 13,
	//	12, 14, 10, 20,  5, 16, 14,  8,
	//	16, 20,  7, 20, 27, 12, 16,  5,
	//	24, 14, 16,  9,  5, 14, 16, 27,
	//	 5,  1,  7,  8,  8, 11, 16, 12,
	//	13, 17, 19, 21, 22,  8,  6, 14,
	//	19,  3,  5, 20,  5, 22, 22, 19,
	//	27, 24, 19, 24,  7, 12,  3, 16,
	//};
	auto src=new short[imsize];//generate test image
	for(;;)
	{
		for(int k=0;k<imsize;++k)
			src[k]=15+(rand()&15)-(rand()&15);

	/*	int bw=1024, bh=1024, imsize=bw*bh, depth=10, nLevels=1<<depth, nLevels_printed=128, bayer='G'|'R'<<8|'B'<<16|'G'<<24;
		auto src=new short[imsize];
		for(int k=0;k<imsize;++k)
			src[k]=63+(rand()&63)-(rand()&63);
		//	src[k]=63+hamming_weight(rand())-hamming_weight(rand());
		//const int spread=(1<<6)-1;
		//for(int ky=0;ky<bh;++ky)
		//	for(int kx=0;kx<bw;++kx)
		//		src[bw*ky+kx]=spread+(rand()&spread)-(rand()&spread);//*/

		printf("Before compression:\n");
		print_subimage(src, bw, bh, 0, 0, 8, 8, 2);//
		//print_subimage(src, bw, bh, bw>>1, bh>>1, 16, 16, 2);//
		//print_subimage(src, bw, bh, 0, 0, bw, bh, 2);//
		int *histogram=new int[nLevels];
		calculate_histogram(src, imsize, histogram, nLevels);
		print_histogram(histogram, nLevels_printed, imsize);//
		//console_pause();//

		std::vector<int> data;
		compress_huff(src, bw, bh, depth, bayer, data);			//compress test image

#if 0
		auto header=(HuffHeader*)data.data();
		auto hData=(HuffDataHeader*)(header->histogram+header->nLevels);
		printf("Compressed bits:\n");
		print_bin((byte*)hData->data, (int)((hData->cBitSize>>3)+((hData->cBitSize&7)!=0)));//
		printf("\n");
#endif

		short *dst=nullptr;
		int w2=0, h2=0, depth2=0;
		char bayer_sh[4];
		decompress_huff((byte*)data.data(), data.size()*sizeof(int), RF_I16_BAYER, (void**)&dst, w2, h2, depth2, bayer_sh);//decompress test image
	
		//printf("After decompression:\n");
		//print_subimage(dst, bw, bh, bw>>1, bh>>1, 16, 16, 2);//
		//print_subimage(dst, bw, bh, 0, 0, bw, bh, 2);
		//calculate_histogram(dst, imsize, histogram, nLevels);
		//print_histogram(histogram, nLevels_printed, imsize);//

		compare_images(src, dst, bw, bh, 1000);
	
		console_pause();
		free(dst);
		delete[] src;
		delete[] histogram;
	}
	console_end();
}

//short			*buffer0=nullptr;
void			archiver_test2()
{
	console_start_good();
	if(!image)
	{
		printf("No image\n");
		console_pause();
		console_end();
		return;
	}
	if(imagetype!=IM_GRAYSCALE&&imagetype!=IM_BAYER&&imagetype!=IM_BAYER_SEPARATE)
	{
		printf("Need Bayer mosaic for archiver test\n");
		console_pause();
		console_end();
		return;
	}
	//console_start();
	printf("\nArchiver test 2\n");

	int bw=iw, bh=ih;
	short *src=new short[image_size];
	int maxlum=(1<<idepth)-1;
	for(int k=0;k<image_size;++k)
		src[k]=short(image[k]*maxlum);

	//int bw=8, bh=8;
	//short src[]=
	//{
	//	 54, 61, 64, 68, 56, 52, 57, 69,
	//	 76, 64, 66, 75, 66, 77, 79, 67,
	//	 62, 60, 70, 47, 62, 62, 57, 74,
	//	 74, 68, 42, 63, 72, 55, 73, 61,
	//	 70, 68, 75, 68, 69, 59, 76, 64,
	//	 64, 72, 58, 72, 57, 66, 59, 81,
	//	 80, 64, 60, 55, 66, 59, 69, 63,
	//	 63, 51, 72, 72, 57, 60, 83, 61,
	//};

	//buffer0=src;

	//printf("Sample from image:\n");
	//print_subimage(src, bw, bh, 0, 0, 8, 8, 2);//

	std::vector<int> data;
	int mosaic='G'|'R'<<8|'B'<<16|'G'<<24;
	//int mosaic=imagetype==IM_GRAYSCALE?0:'G'|'R'<<8|'B'<<16|'G'<<24;
	int data_start=huff::compress_v3(src, bw, bh, idepth, mosaic, data);
	//int data_start=huff::compress(src, bw, bh, idepth, mosaic, data);
		
	auto header=(HuffHeader*)data.data();
	auto hData=(HuffDataHeader*)(header->histogram+header->nLevels);

	short *dst=nullptr;
	int w2=0, h2=0, depth2=0;
	char bayer_sh[4];
	huff::decompress((byte*)data.data(), data.size()*sizeof(int), RF_I16_BAYER, (void**)&dst, w2, h2, depth2, bayer_sh);
		
	if(!dst)
		printf("\nDecompression failed\n");
	else
	{
		bool identical=compare_images(src, dst, bw, bh, 1000);
		if(identical)
		{
			printf("\ndecompress(compress(image)) == image\n");
			//printf("Buffer passed through compression and decompression\n");
			int uncompressed=bw*bh*idepth, compressed=data.size()<<5;
			printf("Uncompressed = %d bits = %g KB\n", uncompressed, (double)uncompressed/(8*1024));
			printf("Compressed   = %d bits = %g KB\n", compressed, (double)compressed/(8*1024));
			printf("Compression ratio = %g\n", (double)uncompressed/compressed);
		}
	}

	console_pause();
	console_end();

	delete[] src;
	free(dst);
}