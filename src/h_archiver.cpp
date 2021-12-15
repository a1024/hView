#include		"generic.h"
#include		"hview.h"
#include		"huff.h"
#include		"vector_bool.h"
#include		<vector>
#include		<queue>
#include		<stack>
#include		<assert.h>
//#include		<tmmintrin.h>
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
void			print_histogram(int *histogram, int nlevels, int scanned_size, int *sort_idx, bool CDF)
{
	int histmax=0;
	for(int k=0;k<nlevels;++k)
		if(histmax<histogram[k])
			histmax=histogram[k];
	if(!histmax)
		return;

	const int consolewidth=99;//79 119
	if(CDF)
	{
		const int consolechars=consolewidth-26-5*(sort_idx!=0);
		if(sort_idx)
			print("idx, ");
		printf("symbol, freq, %%, CDF, %%\n");
		int sum=0;
		for(int k=0;k<nlevels;++k)
		{
			int symbol=sort_idx?sort_idx[k]:k;
			if(!histogram[symbol])
				continue;
			sum+=histogram[symbol];
			if(sort_idx)
				print("%4d ", k);
			printf("%4d %6d %2d %7d %2d ", symbol, histogram[symbol], 100*histogram[symbol]/scanned_size, sum, 100*sum/scanned_size);
			for(int kr=0, count=histogram[symbol]*consolechars/histmax;kr<count;++kr)
				printf("*");
			printf("\n");
		}
	}
	else
	{
		const int consolechars=consolewidth-15-5*(sort_idx!=0);
		if(sort_idx)
			print("idx, ");
		printf("symbol, freq, %%\n");
		for(int k=0;k<nlevels;++k)
		{
			int symbol=sort_idx?sort_idx[k]:k;
			if(!histogram[symbol])
				continue;
			if(sort_idx)
				print("%4d ", k);
			printf("%4d %6d %2d ", symbol, histogram[symbol], 100*histogram[symbol]/scanned_size);
			for(int kr=0, count=histogram[symbol]*consolechars/histmax;kr<count;++kr)
				printf("*");
			printf("\n");
		}
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
		if(image[k]<0||image[k]>=nLevels)
			continue;
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
	print_histogram((int*)header->histogram, header->nLevels, hData->uPxCount, nullptr);
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
		for(int kx=0;kx<dx;++kx)
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
		print_histogram(histogram, nLevels_printed, imsize, nullptr);//
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
	int data_start=huff::compress_v5(src, bw, bh, idepth, mosaic, data);
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


static void		print_subimage_double(const double *buffer, int bw, int bh, int x0, int y0, int dx, int dy)
{
	for(int ky=0;ky<dy;++ky)
	{
		for(int kx=0;kx<dx;++kx)
			printf(" %g", buffer[bw*(y0+ky)+x0+kx]);
			//printf(" %*.*lf", digits, decimals, buffer[bw*(y0+ky)+x0+kx]);
		printf("\n");
	}
	printf("\n");
}
static void		print_matrix(const double *matrix, int bw, int bh)
{
	for(int ky=0;ky<bh;++ky)
	{
		for(int kx=0;kx<bw;++kx)
			printf("%g, ", matrix[bw*ky+kx]);
		printf(";\n");
	}
	printf("\n");
}
short*			dwt_1d(const short *buffer, int count, double **pdata)//power of 2
{
	double *t0=new double[count], *t1=new double[count];
	for(int k=0;k<count;++k)
		t0[k]=buffer[k];
	for(int len=count>>1;;len>>=1)
	{
		for(int k=0;k<len;++k)
		{
			t1[k    ]=0.5*(t0[k<<1]+t0[(k<<1)+1]);
			t1[k+len]=0.5*(t0[k<<1]-t0[(k<<1)+1]);
		}
		if(len==1)
			break;
		memcpy(t0, t1, len<<3);
	}
	delete[] t0;
	if(pdata)
	{
		*pdata=t1;
		return nullptr;
	}
	short *dst=new short[count];
	for(int k=0;k<count;++k)
		dst[k]=(short)t1[k];
	delete[] t1;
	return dst;
}

void			wavelib_dwt_sym_stride_matrix(double *inp, int N, const double *lpd, const double *hpd, int lpd_len, double *cA, int len_cA, double *cD, int istride, int ostride, double *matrix)
{//len_cA = N/2
	int i, l, t, len_avg;
	int is, os;
	int ki, fullsize;

	fullsize=len_cA*2;
	memset(matrix, 0, fullsize*fullsize<<3);
	len_avg=lpd_len;
	for(i=0;i<len_cA;++i)
	{
		t=2*i+1;
		os=i*ostride;
		cA[os]=0;
		cD[os]=0;
		for(l=0;l<len_avg;++l)
		{
			ki=t-l;
			if(ki>=0&&ki<N)
				is=ki;
			else if(ki<0)
				is=-ki-1;
			else if(ki>=N)
				is=2*N-ki-1;

			matrix[N* i			+is]+=lpd[l];
			matrix[N*(i+len_cA)	+is]+=hpd[l];

			is*=istride;

			cA[os] += lpd[l] * inp[is];
			cD[os] += hpd[l] * inp[is];
		}
	}
}
void			wavelib_idwt_sym_stride_matrix(double *cA, int len_cA, double *cD, const double *lpr, const double *hpr, int lpr_len, double *X, int istride, int ostride, double *matrix)
{
	int len_avg, i, l, m, n, t, v;
	int ms, ns, is;
	int ki, fullsize;
	
	fullsize=len_cA*2;
	memset(matrix, 0, fullsize*fullsize<<3);
	len_avg=lpr_len;
	m=-2;
	n=-1;

	for (v = 0; v < len_cA; ++v)
	{
		i=v;
		m+=2;
		n+=2;
		ms=m*ostride;
		ns=n*ostride;
		X[ms]=0;
		X[ns]=0;
		for(l=0;l<len_avg/2;++l)
		{
			t=2*l;
			ki=i-l;
			if(ki>=0&&ki<len_cA)
			{
				is=ki*istride;

				matrix[fullsize*m+ki		]+=lpr[t  ];
				matrix[fullsize*m+ki+len_cA	]+=hpr[t  ];
				matrix[fullsize*n+ki		]+=lpr[t+1];
				matrix[fullsize*n+ki+len_cA	]+=hpr[t+1];

				X[ms]+=lpr[t  ]*cA[is]+hpr[t  ]*cD[is];
				X[ns]+=lpr[t+1]*cA[is]+hpr[t+1]*cD[is];
			}
		}
	}
}
void			wavelib_dwt_sym_stride(double *inp, int N, double *lpd, double *hpd, int lpd_len, double *cA, int len_cA, double *cD, int istride, int ostride)
{
	int i, l, t, len_avg;
	int is, os;
	len_avg = lpd_len;
	for (i = 0; i < len_cA; ++i)
	{
		t = 2 * i + 1;
		os = i *ostride;
		cA[os] = 0.0;
		cD[os] = 0.0;
		for (l = 0; l < len_avg; ++l)
		{
			if ((t - l) >= 0 && (t - l) < N)
			{
				is = (t - l) * istride;
				cA[os] += lpd[l] * inp[is];
				cD[os] += hpd[l] * inp[is];
			}
			else if ((t - l) < 0)
			{
				is = (-t + l - 1) * istride;
				cA[os] += lpd[l] * inp[is];
				cD[os] += hpd[l] * inp[is];
			}
			else if ((t - l) >= N)
			{
				is = (2 * N - t + l - 1) * istride;
				cA[os] += lpd[l] * inp[is];
				cD[os] += hpd[l] * inp[is];
			}
		}
	}
}
void			wavelib_idwt_sym_stride(double *cA, int len_cA, double *cD, double *lpr, double *hpr, int lpr_len, double *X, int istride, int ostride)
{
	int len_avg, i, l, m, n, t, v;
	int ms, ns, is;
	len_avg = lpr_len;
	m = -2;
	n = -1;

	for (v = 0; v < len_cA; ++v)
	{
		i = v;
		m += 2;
		n += 2;
		ms = m * ostride;
		ns = n * ostride;
		X[ms] = 0.0;
		X[ns] = 0.0;
		for (l = 0; l < len_avg / 2; ++l)
		{
			t = 2 * l;
			if ((i - l) >= 0 && (i - l) < len_cA)
			{
				is = (i - l) * istride;
				X[ms] += lpr[t] * cA[is] + hpr[t] * cD[is];
				X[ns] += lpr[t + 1] * cA[is] + hpr[t + 1] * cD[is];
			}
		}
	}
}
void			dwt_1d_matrix(double *image, double *temp, int stride, int count, const double *lpf, const double *hpf, int filtersize, double *matrix)
{
	memset(matrix, 0, count*count<<3);
	int c2=(count>>1)*stride;
	int p2=count-1, period=p2<<1;
	int halffiltersize=filtersize>>1;
	for(int k=0;k<count;k+=2)
	{
		int d_idx=(k>>1)*stride;
		double lo=0, hi=0;
		for(int kc=0;kc<filtersize;++kc)
		{
			int s_idx=k-(kc-halffiltersize);

			if(s_idx<0||s_idx>p2)//zero padding
				continue;
			//s_idx=abs(s_idx)%period;//symmetric padding
			//if(s_idx>p2)
			//	s_idx=period-s_idx;

			matrix[count* (k>>1)			+s_idx]+=lpf[kc];
			matrix[count*((k>>1)+(count>>1))+s_idx]+=hpf[kc];

			auto val=image[s_idx*stride];
			//printf("  [%d] %X", kc-halffiltersize, s_idx*stride);
			lo+=lpf[kc]*val;
			hi+=hpf[kc]*val;
		}
		temp[d_idx   ]=lo;
		temp[d_idx+c2]=hi;
		//printf("\n");//
	}
	if(stride==1)
		memcpy(image, temp, count<<3);
	else
	{
		for(int k=0, kend=count*stride;k<kend;k+=stride)
			image[k]=temp[k];
	}
}
void			idwt_1d_matrix(double *image, double *temp, int stride, int count, const double *lpf, const double *hpf, int filtersize, double *matrix)
{
	memset(matrix, 0, count*count<<3);
	int halfcount=count>>1;
	int c2=halfcount*stride;
	int p2=count-1, period=p2<<1;
	int halffiltersize=filtersize>>1;
	for(int k=0, kd=0;k<count;++k, kd+=stride)
	{
		double sample=0;
		for(int kc=0;kc<filtersize;++kc)
		{
			int s_idx=k-(kc-halffiltersize);
			if(s_idx&1)
				continue;
			
			if(s_idx<0||s_idx>p2)//zero padding
				continue;
			//s_idx=abs(s_idx)%period;//symmetric padding
			//if(s_idx>p2)
			//	s_idx=period-s_idx;

			s_idx>>=1;

			matrix[count*kd+s_idx]+=lpf[kc];
			matrix[count*kd+halfcount+s_idx]+=hpf[kc];

			//printf("  [%d] %X %X", kc-halffiltersize, s_idx*stride, (halfcount+s_idx)*stride);//
			sample+=lpf[kc]*image[s_idx*stride]+hpf[kc]*image[(halfcount+s_idx)*stride];
		}
		//printf("\n");//
		temp[kd]=sample;
	}
	if(stride==1)
		memcpy(image, temp, count<<3);
	else
	{
		for(int k=0, kend=count*stride;k<kend;k+=stride)
			image[k]=temp[k];
	}
}
void			dwt_1d(double *image, double *temp, int stride, int count, const double *lpf, const double *hpf, int filtersize)
{
	int c2=(count>>1)*stride;
	int p2=count-1, period=p2<<1;
	int halffiltersize=filtersize>>1;
	for(int k=0;k<count;k+=2)
	{
		double lo=0, hi=0;
		for(int kc=0;kc<filtersize;++kc)
		{
			int s_idx=k-(kc-halffiltersize);

			//if(s_idx<0||s_idx>p2)//zero padding
			//	continue;
			s_idx=abs(s_idx)%period;//symmetric padding
			if(s_idx>p2)
				s_idx=period-s_idx;

			auto val=image[s_idx*stride];
			//printf("  [%d] %X", kc-halffiltersize, s_idx*stride);
			lo+=lpf[kc]*val;
			hi+=hpf[kc]*val;
		}
		int d_idx=(k>>1)*stride;
		temp[d_idx   ]=lo;
		temp[d_idx+c2]=hi;
		//printf("\n");//
	}
	if(stride==1)
		memcpy(image, temp, count<<3);
	else
	{
		for(int k=0, kend=count*stride;k<kend;k+=stride)
			image[k]=temp[k];
	}
}
void			idwt_1d(double *image, double *temp, int stride, int count, const double *lpf, const double *hpf, int filtersize)
{
	int halfcount=count>>1;
	int c2=halfcount*stride;
	int p2=count-1, period=p2<<1;
	int halffiltersize=filtersize>>1;
	for(int k=0, kd=0;k<count;++k, kd+=stride)
	{
		double sample=0;
		for(int kc=0;kc<filtersize;++kc)
		{
			int s_idx=k-(kc-halffiltersize);
			if(s_idx&1)
				continue;
			
			//if(s_idx<0||s_idx>p2)//zero padding
			//	continue;
			s_idx=abs(s_idx)%period;//symmetric padding
			if(s_idx>p2)
				s_idx=period-1-s_idx;

			s_idx>>=1;

			//printf("  [%d] %X %X", kc-halffiltersize, s_idx*stride, (halfcount+s_idx)*stride);//
			sample+=lpf[kc]*image[s_idx*stride]+hpf[kc]*image[(halfcount+s_idx)*stride];
		}
		//printf("\n");//
		temp[kd]=sample;
	}
	if(stride==1)
		memcpy(image, temp, count<<3);
	else
	{
		for(int k=0, kend=count*stride;k<kend;k+=stride)
			image[k]=temp[k];
	}
}
/*void			dwt_row(double *src, double *dst, int count, const double *lpf, const double *hpf, int filtersize)
{
	int c2=count>>1;
	for(int k=0;k<count;k+=2)
	{
		double lo=0, hi=0;
		for(int kc=1-filtersize;kc<filtersize;++kc)
		{
			int s_idx=count-1-abs(count-1-abs(k+kc));
			auto val=src[s_idx];
			int f_idx=abs(kc);
			lo+=lpf[f_idx]*val;
			hi+=hpf[f_idx]*val;
		}
		int d_idx=k>>1;
		dst[d_idx   ]=lo;
		dst[d_idx+c2]=hi;
	}
}//*/
short*			dwt_2d(const void *buffer, bool isDouble, int width, int height, const double *lpf, const double *hpf, int filtersize, bool forward, double **pdata)//power of 2
{
	int imsize=width*height;
	double *t0=new double[imsize], *t1=new double[imsize];
	if(isDouble)
		memcpy(t0, buffer, imsize<<3);
	else
	{
		auto p=(const short*)buffer;
		for(int k=0;k<imsize;++k)
			t0[k]=p[k];
	}
	if(forward)
	{
		int w2=width, h2=height;
		for(;w2>1;)
		{
			for(int ky=0;ky<height;++ky)
				dwt_1d(t0+width*ky, t1+width*ky, 1, w2, lpf, hpf, filtersize);
			w2>>=1;
		}
		for(;h2>1;)
		{
			for(int kx=0;kx<width;++kx)
				dwt_1d(t0+kx, t1+kx, width, h2, lpf, hpf, filtersize);
			h2>>=1;
		}
	}
	else
	{
		int w2=2, h2=2;
		for(;h2<=height;)
		{
			for(int kx=0;kx<width;++kx)
				idwt_1d(t0+kx, t1+kx, width, h2, lpf, hpf, filtersize);
			h2<<=1;
		}
		for(;w2<=width;)
		{
			for(int ky=0;ky<height;++ky)
				idwt_1d(t0+width*ky, t1+width*ky, 1, w2, lpf, hpf, filtersize);
			w2<<=1;
		}
	}
	delete[] t1;
	if(pdata)
	{
		*pdata=t0;
		return nullptr;
	}
	short *dst=new short[imsize];
	for(int k=0;k<imsize;++k)
		dst[k]=(short)t0[k];
	delete[] t0;
	return dst;
}

inline void		PLHaar(short a, short b, short bias, short &lo, short &hi)
{
	short s=a<bias, t=b<bias;
	a+=s, b+=t;//asymmetry: nudge origin to (+0, +0)
	if(s==t)//A*B>0?
	{
		a-=b-bias;//H=A-B
		if(a<bias==s)//|A|>|B|?
			b+=a-bias;//L=A (replaces L=B)
	}
	else//A*B<0?
	{
		b+=a-bias;//L=A+B
		if(b<bias==t)//|B|>|A|?
			a-=b-bias;//H=-B (replaces H=A)
	}
	a-=s, b-=t;//asymmetry: restore origin
	lo=b, hi=a;
}
void			PLHaar2D(const short *src, int width, int height, int bias, short *dst)
{
	int imsize=width*height;
	short *t0=new short[imsize], *t1=new short[imsize];
	memcpy(t0, src, imsize<<1);
	for(int w2=width;w2>1;)
	{
		w2>>=1;
		for(int ky=0;ky<height;++ky)
		{
			short *srow=t0+width*ky, *drow=t1+width*ky;
			for(int kx=0;kx<w2;++kx)
				PLHaar(srow[kx<<1], srow[(kx<<1)+1], bias, drow[kx], drow[w2+kx]);
		}
		std::swap(t0, t1);
	}
	for(int h2=height;h2>1;)
	{
		h2>>=1;
		for(int kx=0;kx<width;++kx)
		{
			short *scol=t1+kx, *dcol=t0+kx;
			for(int ky=0, hw=width*h2;ky<hw;ky+=width)
				PLHaar(scol[ky<<1], scol[(ky<<1)+1], bias, dcol[ky], dcol[hw+ky]);
		}
		std::swap(t0, t1);
	}
	memcpy(dst, t0, imsize<<1);
/*	int buffer=0;
	for(int w2=width, h2=height;;)
	{
		w2>>=1;
		if(!w2)
			break;
		for(int ky=0;ky<height;++ky)
		{
			short *srow=t0+width*ky, *drow=t1+width*ky;
			for(int kx=0;kx<w2;++kx)
				PLHaar(srow[kx<<1], srow[(kx<<1)+1], bias, drow[kx], drow[w2+kx]);
		}
		h2>>=1;
		if(!h2)
		{
			buffer=1;
			break;
		}
		for(int kx=0;kx<width;++kx)
		{
			short *scol=t1+kx, *dcol=t0+kx;
			for(int ky=0, hw=width*h2;ky<hw;ky+=width)
				PLHaar(scol[ky<<1], scol[(ky<<1)+1], bias, dcol[ky], dcol[hw+ky]);
		}
	}
	memcpy(dst, buffer?t1:t0, imsize<<1);//*/
	delete[] t0, t1;
}
void			invPLHaar2D(const short *src, int width, int height, int bias, short *dst)
{
	int imsize=width*height;
	short *t0=new short[imsize], *t1=new short[imsize];
	memcpy(t0, src, imsize<<1);
	for(int h2=1;h2<height;h2<<=1)
	{
		for(int kx=0;kx<width;++kx)
		{
			short *scol=t1+kx, *dcol=t0+kx;
			for(int ky=0, hw=width*h2;ky<hw;ky+=width)
				PLHaar(scol[ky<<1], scol[(ky<<1)+1], bias, dcol[ky], dcol[hw+ky]);
		}
		std::swap(t0, t1);
	}
	for(int w2=1;w2<width;w2<<=1)
	{
		for(int ky=0;ky<height;++ky)
		{
			short *srow=t0+width*ky, *drow=t1+width*ky;
			for(int kx=0;kx<w2;++kx)
				PLHaar(srow[kx<<1], srow[(kx<<1)+1], bias, drow[kx], drow[w2+kx]);
		}
		std::swap(t0, t1);
	}
	memcpy(dst, t0, imsize<<1);
	delete[] t0, t1;
}

//temp: of size ceil(count*0.5)*3 shorts
//filter: {alpha[-1], alpha[0], alpha[1], beta, log2(denomenator)}
void			ICER_DWT_run(short *buffer, short *temp, const short *filter, int stride, int count)
{
	assert(count>=3);
	int odd=count&1, nhi=count>>1, nlo=count-nhi;
	short *l=temp, *d=temp+nlo, *r=temp+(nlo<<1)-1;//r starts from 1
	int stride2=stride<<1;

	for(int kd=0, ks=0;kd<nlo;++kd, ks+=stride2)//calculate low-pass outputs
		l[kd]=(buffer[ks]+buffer[ks+stride])>>1;
	if(odd)
		l[nhi]=buffer[(count-1)*stride];
	
	for(int kd=0, ks=0;kd<nlo;++kd, ks+=stride2)//calculate d
		d[kd]=buffer[ks]-buffer[ks+stride];
	if(odd)
		d[nhi]=0;

	for(int k=1;k<nlo;++k)//calculate r
		r[k]=l[k-1]-l[k];

	short *h=buffer+stride*nlo;//calculate high-pass output
	int halfden=1<<(filter[4]-1);
	int t=r[1];
	h[0]=d[0]-(t>>2);
	int even=!odd, hiloopcount=nhi-even;
	if(filter[0])
	{
		t=(r[1]<<1)+r[2]*3-(d[2]<<1)+4;
		h[stride]=d[1]-(t>>3);
		for(int kd=stride2, ks=2;ks<hiloopcount;kd+=stride, ++ks)
		{
			t=filter[0]*r[ks-1]+filter[1]*r[ks]+filter[2]*r[ks+1]-filter[3]*d[ks+1]+halfden;
			h[kd]=d[ks]-(t>>filter[4]);
		}
	}
	else//filter[0]==0
	{
		for(int kd=stride, ks=1;ks<hiloopcount;kd+=stride, ++ks)
		{
			t=filter[1]*r[ks]+filter[2]*r[ks+1]-filter[3]*d[ks+1]+halfden;
			h[kd]=d[ks]-(t>>filter[4]);
		}
	}
	if(even)
	{
		t=r[nhi-1];
		h[(nhi-1)*stride]=d[nhi-1]-(t>>2);
	}

	if(stride==1)
		memcpy(buffer, l, nlo<<1);
	else
	{
		for(int kd=0, ks=0;ks<nlo;kd+=stride, ++ks)//copy low-pass output
			buffer[kd]=l[ks];
	}
}
void			ICER_IDWT_run(short *buffer, short *temp, const short *filter, int stride, int count)
{
	assert(count>=3);
	int odd=count&1, nhi=count>>1, nlo=count-nhi;
	short *h=buffer+stride*nlo;
	short *l=temp, *d=temp+nlo, *r=temp+(nlo<<1)-1;//r starts from 1
	int stride2=stride<<1;

	for(int kd=0, ks=0;kd<nlo;++kd, ks+=stride)//copy low-pass info, last loop needs l but it overwrites it at double rate
		l[kd]=buffer[ks];

	for(int k=1;k<nlo;++k)//calculate r
		r[k]=l[k-1]-l[k];

	int even=!odd;
	int t;
	int halfden=1<<(filter[4]-1);//calculate d
	if(odd)
		d[nlo-1]=0;
	else
	{
		t=r[nhi-1];
		d[nlo-1]=h[stride*(nhi-1)]+(t>>2);
	}
	if(filter[0])
	{
		for(int kd=nlo-2, ks=stride*kd;kd>=2;--kd, ks-=stride)
		{
			t=filter[0]*r[kd-1]+filter[1]*r[kd]+filter[2]*r[kd+1]-filter[3]*d[kd+1]+halfden;
			d[kd]=h[ks]+(t>>filter[4]);
		}
		t=(r[1]<<1)+r[2]*3-(d[2]<<1)+4;
		d[1]=h[stride]+(t>>3);
	}
	else//filter[0]==0
	{
		for(int kd=nlo-2, ks=stride*kd;kd>=1;--kd, ks-=stride)
		{
			t=filter[1]*r[kd]+filter[2]*r[kd+1]-filter[3]*d[kd+1]+halfden;
			d[kd]=h[ks]+(t>>filter[4]);
		}
	}
	t=r[1];
	d[0]=h[0]+(t>>2);

	for(int kd=0, ks=0;ks<nhi;++ks, kd+=stride2)//recover original data
	{
		t=d[ks]+1;
		buffer[kd]=l[ks]+(t>>1);
		buffer[kd+stride]=buffer[kd]-d[ks];
	}
	if(odd)
		buffer[stride*(count-1)]=l[nlo-1];
		//buffer[stride*(count-1)]=l[nlo-1]+((d[nlo-1]+1)>>2);//d[nlo-1]==0
}
void			ICER_DWT1D(short *buffer, int count, ICER_FilterType filtertype, int nstages)
{
	const short *filter=ICER_filters+5*filtertype;
	int tsize=(count<<1)-(count>>1);
	short *temp=new short[tsize];
	for(int w2=count, it=0;w2>=3&&(!nstages||it<nstages);++it)
	{
		ICER_DWT_run(buffer, temp, filter, 1, w2);

		w2-=w2>>1;//w=ceil(w/2)
	}
	delete[] temp;
}
void			ICER_IDWT1D(short *buffer, int count, ICER_FilterType filtertype, int nstages)
{
	const short *filter=ICER_filters+5*filtertype;

	int lw=floor_log2(count);
	short *sizes=new short[lw];
	int nsizes=0;
	for(int w2=count;w2>=3&&(!nstages||nsizes<nstages);++nsizes)//calculate size of each stage
	{
		sizes[nsizes]=w2;
		w2-=w2>>1;//w=ceil(w/2)
	}
	
	int tsize=count;
	tsize=(tsize<<1)-(tsize>>1);//tsize=ceil(1.5*max(w, h))
	short *temp=new short[tsize];
	for(int it=nsizes-1;it>=0;--it)
		ICER_IDWT_run(buffer, temp, filter, 1, sizes[it]);
	delete[] sizes, temp;
}
void			ICER_DWT2D(short *buffer, int bw, int bh, ICER_FilterType filtertype, int nstages)
{
	const short *filter=ICER_filters+5*filtertype;
	int tsize=maximum(bw, bh);
	tsize=(tsize<<1)-(tsize>>1);//tsize=ceil(1.5*max(w, h))
	short *temp=new short[tsize];
	for(int w2=bw, h2=bh, it=0;w2>=3&&h2>=3&&(!nstages||it<nstages);++it)
	{
		for(int ky=0;ky<h2;++ky)//horizontal DWT
			ICER_DWT_run(buffer+bw*ky, temp, filter, 1, w2);

		//printf("ICER DWT %d:\n", it);//
		//print_subimage(buffer, bw, bh, 0, 0, bw, bh, 4);//

		for(int kx=0;kx<w2;++kx)//vertical DWT
			ICER_DWT_run(buffer+kx, temp, filter, bw, h2);

		w2-=w2>>1;//w=ceil(w/2)
		h2-=h2>>1;//h=ceil(h/2)
	}
	delete[] temp;
}
void			ICER_IDWT2D(short *buffer, int bw, int bh, ICER_FilterType filtertype, int nstages)
{
	const short *filter=ICER_filters+5*filtertype;

	int lw=floor_log2(bw), lh=floor_log2(bh);
	short *sizes=new short[minimum(lw, lh)<<1];
	int nsizes=0;
	for(int w2=bw, h2=bh;w2>=3&&h2>=3&&(!nstages||nsizes<nstages);++nsizes)//calculate dimensions of each stage
	{
		sizes[nsizes<<1]=w2;
		sizes[(nsizes<<1)+1]=h2;
		w2-=w2>>1;//w=ceil(w/2)
		h2-=h2>>1;//h=ceil(h/2)
	}
	
	int tsize=maximum(bw, bh);
	tsize=(tsize<<1)-(tsize>>1);//tsize=ceil(1.5*max(w, h))
	short *temp=new short[tsize];
	for(int it=nsizes-1;it>=0;--it)
	{
		int w2=sizes[it<<1], h2=sizes[(it<<1)+1];

		for(int kx=0;kx<w2;++kx)//vertical IDWT
			ICER_IDWT_run(buffer+kx, temp, filter, bw, h2);

		//printf("ICER IDWT %d:\n", it);//
		//print_subimage(buffer, bw, bh, 0, 0, bw, bh, 4);//

		for(int ky=0;ky<h2;++ky)//horizontal IDWT
			ICER_IDWT_run(buffer+bw*ky, temp, filter, 1, w2);
	}
	delete[] sizes, temp;
}

void			encode_zigzag(short *buffer, int imsize)
{
	for(int k=0;k<imsize;++k)
	{
		auto &symbol=buffer[k];
		int neg=symbol<0;
		symbol=abs(symbol+neg)<<1|neg;
	}
}
void			decode_zigzag(short *buffer, int imsize)
{
	for(int k=0;k<imsize;++k)
	{
		auto &symbol=buffer[k];
		int negative=symbol&1;
		symbol=symbol>>1^-negative;
	}
}
void			archiver_test3()
{
	console_start_good();
#if 1
	if(!image)
	{
		printf("Please open an image first\n");
		console_pause();
		console_end();
		return;
	}
#define	HEAP_SRC
	short *src=new short[image_size];
	int normal=1<<idepth;
	for(int k=0;k<image_size;++k)
		src[k]=(short)floor(image[k]*normal+0.5);
	int width=iw, height=ih, depth=idepth, imsize=image_size;
#endif
#if 0
	const int width=16, height=16, depth=12;
	//const int width=16, height=16, depth=4;
	//const int width=4, height=4, depth=4;
	//const int width=4, height=1, depth=4;
	//const int width=16, height=1, depth=4;
	//const int width=16, height=16, depth=4;
	//const int width=16, height=1, depth=12;
	//const int width=44, height=23, depth=12;
	short src[]=
	{
		//1, 2, 3, 4,
		//5, 6, 7, 8,
		//9, 10, 11, 12,
		//13, 14, 15, 16,

		//1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
		//1, 2, 3, 4, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 3, 4, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 5, 9, 13, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//2, 6, 10, 14, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 3, 5, 7, 9, 11, 13, 15, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,

		  411,  442,  462,  457,  441,  446,  464,  505,  500,  581,  800, 1074, 1195, 1288, 1398, 1519,
		  409,  443,  433,  431,  426,  431,  450,  544,  568,  652,  896, 1134, 1202, 1310, 1394, 1428,
		  415,  434,  406,  412,  423,  445,  496,  544,  603,  806, 1072, 1286, 1398, 1482, 1463, 1372,
		  410,  402,  392,  419,  443,  467,  479,  580,  668,  918, 1241, 1386, 1468, 1520, 1458, 1637,
		  390,  392,  422,  447,  464,  494,  542,  624,  836, 1173, 1410, 1520, 1472, 1523, 1717, 1893,
		  395,  382,  424,  452,  483,  504,  584,  676, 1151, 1693, 1873, 1808, 1715, 1831, 1938, 1919,
		  393,  383,  400,  451,  507,  543,  617, 1090, 2421, 2964, 2653, 2119, 1835, 1848, 1973, 1984,
		  390,  393,  407,  455,  513,  574,  675, 2234, 3593, 3359, 2592, 1942, 1768, 1936, 1959, 1990,
		  398,  396,  420,  490,  530,  611,  810, 2101, 2796, 2359, 1881, 1735, 1757, 1802, 1872, 1900,
		  417,  450,  478,  493,  544,  603,  836, 1301, 1568, 1765, 1676, 1701, 1724, 1760, 1700, 1681,
		  424,  474,  492,  501,  530,  562,  777, 1129, 1418, 1541, 1592, 1746, 1769, 1728, 1637, 1598,
		  409,  433,  474,  502,  502,  570,  802, 1203, 1428, 1544, 1557, 1723, 1781, 1692, 1614, 1569,
		  408,  434,  443,  460,  497,  574,  888, 1250, 1417, 1527, 1608, 1614, 1691, 1626, 1563, 1534,
		  432,  434,  467,  476,  509,  629,  940, 1214, 1283, 1405, 1580, 1597, 1582, 1523, 1490, 1550,
		  481,  484,  495,  517,  574,  733,  970, 1154, 1189, 1289, 1456, 1487, 1508, 1541, 1553, 1615,
		  485,  491,  487,  545,  564,  815, 1056, 1167, 1270, 1343, 1416, 1482, 1528, 1544, 1596, 1675,

		//  295,  302,  317,  330,  335,  336,  330,  324,  322,  318,  336,  352,  383,  400,  410,  429,  450,  437,  479,  535,  675,  882, 1000, 1181, 1320, 1345, 1370, 1404, 1361, 1304, 1438, 1500, 1546, 1572, 1638, 1644, 1595, 1613, 1651, 1615, 1543, 1564, 1618, 1664,
		//  296,  301,  324,  334,  337,  330,  326,  335,  336,  337,  360,  389,  421,  437,  469,  448,  471,  477,  505,  541,  677,  913, 1017, 1161, 1267, 1290, 1333, 1339, 1349, 1317, 1385, 1531, 1609, 1587, 1703, 1735, 1613, 1684, 1661, 1742, 1722, 1708, 1688, 1752,
		//  302,  305,  336,  334,  340,  333,  334,  332,  342,  358,  384,  416,  433,  484,  484,  479,  490,  501,  508,  591,  751,  943, 1062, 1209, 1262, 1314, 1388, 1446, 1392, 1381, 1474, 1504, 1558, 1623, 1703, 1748, 1698, 1739, 1742, 1801, 1809, 1761, 1661, 1708,
		//  303,  317,  333,  338,  331,  323,  332,  333,  346,  375,  402,  426,  454,  476,  473,  451,  487,  487,  513,  613,  798, 1026, 1120, 1265, 1390, 1458, 1439, 1498, 1441, 1439, 1499, 1493, 1542, 1557, 1654, 1802, 1874, 1769, 1807, 1896, 1900, 1754, 1682, 1697,
		//  308,  323,  317,  335,  335,  325,  334,  341,  356,  390,  426,  454,  475,  451,  435,  455,  481,  501,  523,  676,  944, 1153, 1226, 1343, 1507, 1533, 1563, 1601, 1657, 1641, 1603, 1507, 1607, 1605, 1640, 1767, 1813, 1767, 1830, 1854, 1795, 1778, 1676, 1659,
		//  319,  327,  321,  337,  330,  340,  340,  369,  377,  411,  442,  462,  457,  441,  446,  464,  505,  500,  581,  800, 1074, 1195, 1288, 1398, 1519, 1528, 1564, 1738, 1891, 1838, 1719, 1636, 1764, 1750, 1733, 1768, 1824, 1801, 1754, 1796, 1786, 1754, 1664, 1665,
		//  321,  335,  332,  321,  334,  335,  345,  357,  387,  409,  443,  433,  431,  426,  431,  450,  544,  568,  652,  896, 1134, 1202, 1310, 1394, 1428, 1546, 1680, 1828, 1983, 1968, 1818, 1807, 1836, 1856, 1896, 1844, 1844, 1873, 1806, 1777, 1722, 1733, 1637, 1564,
		//  323,  338,  324,  344,  337,  341,  356,  366,  398,  415,  434,  406,  412,  423,  445,  496,  544,  603,  806, 1072, 1286, 1398, 1482, 1463, 1372, 1565, 1731, 1882, 1968, 1999, 1971, 1885, 1881, 1978, 1886, 1813, 1886, 1964, 1875, 1806, 1652, 1751, 1598, 1526,
		//  327,  343,  329,  338,  328,  344,  372,  397,  418,  410,  402,  392,  419,  443,  467,  479,  580,  668,  918, 1241, 1386, 1468, 1520, 1458, 1637, 1787, 1843, 1874, 1884, 1896, 1930, 1854, 1947, 1946, 1948, 1901, 1863, 1897, 1886, 1760, 1728, 1660, 1625, 1571,
		//  335,  329,  332,  338,  342,  360,  379,  405,  402,  390,  392,  422,  447,  464,  494,  542,  624,  836, 1173, 1410, 1520, 1472, 1523, 1717, 1893, 1893, 1889, 1733, 1843, 1752, 1723, 1693, 1740, 1803, 1860, 1792, 1813, 1929, 1835, 1816, 1642, 1639, 1597, 1453,
		//  334,  334,  349,  351,  346,  361,  363,  388,  399,  395,  382,  424,  452,  483,  504,  584,  676, 1151, 1693, 1873, 1808, 1715, 1831, 1938, 1919, 1873, 1869, 1809, 1781, 1684, 1638, 1630, 1658, 1657, 1748, 1776, 1801, 1797, 1762, 1732, 1605, 1605, 1480, 1440,
		//  337,  340,  361,  351,  366,  366,  365,  385,  385,  393,  383,  400,  451,  507,  543,  617, 1090, 2421, 2964, 2653, 2119, 1835, 1848, 1973, 1984, 1935, 1860, 1866, 1741, 1603, 1649, 1674, 1677, 1789, 1913, 1871, 1793, 1800, 1769, 1673, 1610, 1550, 1532, 1470,
		//  335,  337,  344,  337,  360,  373,  381,  366,  389,  390,  393,  407,  455,  513,  574,  675, 2234, 3593, 3359, 2592, 1942, 1768, 1936, 1959, 1990, 1913, 1789, 1743, 1626, 1714, 1658, 1774, 1800, 1906, 1988, 1919, 1849, 1856, 1860, 1804, 1586, 1585, 1582, 1482,
		//  332,  338,  351,  336,  352,  352,  368,  378,  385,  398,  396,  420,  490,  530,  611,  810, 2101, 2796, 2359, 1881, 1735, 1757, 1802, 1872, 1900, 1825, 1706, 1698, 1726, 1702, 1769, 1850, 1839, 1932, 1929, 1914, 1847, 1831, 1778, 1639, 1547, 1535, 1524, 1495,
		//  334,  334,  333,  334,  352,  338,  348,  362,  392,  417,  450,  478,  493,  544,  603,  836, 1301, 1568, 1765, 1676, 1701, 1724, 1760, 1700, 1681, 1634, 1561, 1566, 1661, 1654, 1785, 1934, 1831, 1848, 1788, 1835, 1834, 1822, 1791, 1695, 1584, 1579, 1567, 1489,
		//  335,  335,  337,  335,  333,  346,  341,  360,  390,  424,  474,  492,  501,  530,  562,  777, 1129, 1418, 1541, 1592, 1746, 1769, 1728, 1637, 1598, 1475, 1476, 1523, 1568, 1616, 1699, 1836, 1804, 1737, 1761, 1739, 1770, 1827, 1819, 1707, 1694, 1603, 1623, 1628,
		//  348,  336,  329,  329,  327,  325,  340,  349,  379,  409,  433,  474,  502,  502,  570,  802, 1203, 1428, 1544, 1557, 1723, 1781, 1692, 1614, 1569, 1514, 1499, 1556, 1582, 1648, 1706, 1695, 1703, 1748, 1646, 1659, 1777, 1803, 1816, 1850, 1675, 1581, 1520, 1557,
		//  349,  352,  332,  327,  308,  316,  320,  329,  359,  408,  434,  443,  460,  497,  574,  888, 1250, 1417, 1527, 1608, 1614, 1691, 1626, 1563, 1534, 1558, 1609, 1568, 1615, 1622, 1718, 1685, 1667, 1655, 1667, 1630, 1758, 1824, 1877, 1851, 1713, 1591, 1464, 1426,
		//  329,  337,  332,  319,  302,  320,  328,  353,  392,  432,  434,  467,  476,  509,  629,  940, 1214, 1283, 1405, 1580, 1597, 1582, 1523, 1490, 1550, 1591, 1665, 1676, 1718, 1772, 1763, 1706, 1674, 1712, 1696, 1834, 1794, 1815, 1884, 1848, 1766, 1552, 1426, 1359,
		//  335,  338,  340,  334,  330,  332,  372,  409,  455,  481,  484,  495,  517,  574,  733,  970, 1154, 1189, 1289, 1456, 1487, 1508, 1541, 1553, 1615, 1741, 1738, 1720, 1737, 1882, 1922, 1875, 1864, 1793, 1902, 1920, 1867, 1859, 1841, 1827, 1718, 1617, 1430, 1460,
		//  336,  337,  339,  326,  331,  365,  408,  445,  485,  485,  491,  487,  545,  564,  815, 1056, 1167, 1270, 1343, 1416, 1482, 1528, 1544, 1596, 1675, 1847, 1961, 1874, 1870, 1918, 1998, 1965, 1930, 1940, 2008, 1897, 1822, 1752, 1739, 1708, 1706, 1709, 1636, 1617,
		//  338,  341,  343,  339,  362,  395,  463,  494,  480,  480,  483,  500,  547,  671,  938, 1148, 1309, 1442, 1423, 1472, 1468, 1541, 1621, 1596, 1646, 1771, 1922, 1905, 1923, 1976, 1985, 2075, 1978, 1860, 1807, 1799, 1790, 1744, 1743, 1818, 1802, 1829, 1852, 1716,
		//  331,  333,  337,  347,  386,  430,  479,  508,  503,  470,  503,  524,  565,  753,  995, 1271, 1374, 1455, 1519, 1534, 1501, 1556, 1617, 1585, 1634, 1680, 1809, 1938, 1981, 1909, 1877, 1797, 1631, 1662, 1667, 1771, 1731, 1757, 1718, 1793, 1802, 1921, 1863, 1787,

		//54, 61, 64, 68, 56, 52, 57, 69, 65, 69, 64, 49, 66, 67, 62, 69,
		//76, 64, 66, 75, 66, 77, 79, 67, 67, 62, 65, 62, 62, 54, 79, 70,
		//62, 60, 70, 47, 62, 62, 57, 74, 60, 74, 65, 69, 70, 70, 60, 66,
		//74, 68, 42, 63, 72, 55, 73, 61, 73, 73, 66, 57, 67, 48, 56, 66,
		//70, 68, 75, 68, 69, 59, 76, 64, 63, 49, 75, 68, 57, 58, 77, 61,
		//64, 72, 58, 72, 57, 66, 59, 81, 67, 60, 64, 67, 66, 55, 66, 75,
		//80, 64, 60, 55, 66, 59, 69, 63, 72, 62, 62, 72, 73, 61, 83, 64,
		//63, 51, 72, 72, 57, 60, 83, 61, 62, 85, 67, 62, 55, 71, 60, 53,
		//67, 63, 61, 73, 65, 66, 63, 66, 73, 60, 63, 50, 77, 66, 59, 63,
		//69, 64, 78, 68, 60, 66, 72, 61, 75, 71, 66, 60, 56, 65, 66, 67,
		//58, 70, 69, 76, 60, 61, 66, 66, 66, 80, 66, 64, 57, 53, 71, 66,
		//74, 64, 53, 58, 75, 65, 79, 72, 63, 52, 66, 64, 71, 77, 68, 75,
		//69, 60, 76, 83, 64, 57, 71, 64, 64, 64, 66, 64, 73, 52, 62, 60,
		//64, 54, 69, 61, 59, 70, 61, 66, 65, 70, 70, 72, 70, 62, 59, 60,
		//78, 76, 63, 69, 74, 71, 61, 45, 75, 70, 70, 63, 69, 66, 67, 69,
		//66, 56, 68, 54, 75, 80, 80, 71, 66, 71, 71, 64, 62, 67, 67, 83,
	};
	const int imsize=width*height;
#endif

	short *dst=new short[imsize];

	memcpy(dst, src, imsize<<1);

	//ICER DWT
	printf("Top-left corner:\n");
	print_subimage(dst, width, height, 0, 0, 16, 16, 4);
	printf("Bottom-right corner:\n");
	print_subimage(dst, width, height, width-16, height-16, 16, 16, 4);
	ICER_DWT2D(dst, width, height, ICER_FILTER_A);
	//ICER_IDWT2D(dst, width, height, ICER_FILTER_A);
	encode_zigzag(dst, imsize);
	depth+=1;

	//int nstages=3;
	//printf("Src:\n");
	//print_subimage(dst, width, height, 0, 0, width, height, 4);
	//ICER_DWT2D(dst, width, height, ICER_FILTER_A, nstages);
	//printf("ICER DWT:\n");
	//print_subimage(dst, width, height, 0, 0, width, height, 4);
	//ICER_IDWT2D(dst, width, height, ICER_FILTER_A, nstages);
	//printf("ICER IDWT:\n");
	//print_subimage(dst, width, height, 0, 0, width, height, 4);
	
	//int dwtsize=8;
	//printf("Src:\n");
	//print_subimage(dst, width, height, 0, 0, dwtsize, 1, 4);
	//ICER_DWT1D(dst, dwtsize, ICER_FILTER_A, nstages);
	//printf("ICER DWT:\n");
	//print_subimage(dst, width, height, 0, 0, dwtsize, 1, 4);
	//ICER_IDWT1D(dst, dwtsize, ICER_FILTER_A, nstages);
	//printf("ICER IDWT:\n");
	//print_subimage(dst, width, height, 0, 0, dwtsize, 1, 4);

	
	//PLHaar transform
	//PLHaar2D(src, width, height, 1<<(depth-1), dst);
	//PLHaar2D(src, width, height, 0, dst);//
	//PLHaar2D(src, 4, 1, 0, dst);//
	//memcpy(src, dst, imsize<<1);

/*	//left deltas
	//-1 [1]
	for(int ky=0;ky<height;++ky)
	{
		const short *srow=src+width*ky;
		short *drow=dst+width*ky;
		drow[0]=srow[0];
		for(int kx=1;kx<width;++kx)
		{
			auto &symbol=drow[kx];
			symbol=srow[kx]-srow[kx-1];
			int neg=symbol<0;
			symbol=abs(symbol+neg)<<1|neg;
		}
	}
	depth+=1;//*/

/*	//top-left-predictor
	//     -1/2		     1/2
	//-1/2 [1]		1/2 [1]
	for(int kx=0;kx<width;++kx)
		dst[kx]=src[kx];
	for(int ky=1;ky<height;++ky)
	{
		const short *srow=src+width*ky, *srow0=srow-width;
		short *drow=dst+width*ky;
		drow[0]=srow[0];
		for(int kx=1;kx<width;++kx)
		{
			auto &symbol=drow[kx];
			symbol=srow[kx]-((srow0[kx]+srow[kx-1])>>1);

			int neg=symbol<0;
			symbol=abs(symbol+neg)<<1|neg;
		}
	}
	depth+=1;//*/

/*	//top-left, top, top-right, left -predictor
	//forward			inverse
	//-1/4 -1/4 -1/4	1/4  1/4  1/4
	//-1/4 [1]			1/4 [1]
	dst[0]=src[0];
	for(int kx=1;kx<width;++kx)
		dst[kx]=src[kx]-src[kx-1];
	for(int ky=1;ky<height;++ky)
	{
		const short *srow=src+width*ky, *srow0=srow-width;
		short *drow=dst+width*ky;
		drow[0]=srow[0]-((srow0[0]+srow0[1])>>1);
		for(int kx=1;kx<width;++kx)
			drow[kx]=srow[kx]-((srow0[kx-1]+srow0[kx]+srow0[kx+1]+srow[kx-1])>>2);
	}
	encode_zigzag(dst, imsize);
	depth+=1;//*/
	
	//printf("Full image:\n");
	//print_subimage(dst, width, height, 0, 0, width, height, 4);
	printf("Top-left corner:\n");
	print_subimage(dst, width, height, 0, 0, 16, 16, 4);
	printf("Bottom-right corner:\n");
	print_subimage(dst, width, height, width-16, height-16, 16, 16, 4);

	int nlevels=1<<depth;
	int *histogram=new int[nlevels];
	calculate_histogram(dst, imsize, histogram, nlevels);
	std::vector<int> index(nlevels);
	for(int k=0;k<nlevels;++k)
		index[k]=k;
	std::sort(index.begin(), index.end(), [&](int idx1, int idx2){return histogram[idx1]>histogram[idx2];});
	print_histogram(histogram, nlevels, imsize, index.data(), true);

	double centroid=0;
	for(int k=0;k<nlevels;++k)
		centroid+=k*histogram[index[k]];
	centroid/=imsize;
	printf("Expected value from sorted palette = %lf\n", centroid);
	int kcdf=16;
	int integral=0;
	for(int k=0, kend=minimum(kcdf, nlevels);k<kend;++k)
		integral+=histogram[index[k]];
	printf("First %d values from sorted histogram covers %d pixels = %lf%%\n", kcdf, integral, 100.*integral/imsize);//*/

#if 0
	//db1
	//const double sqrt2=0.7071067811865475244008443621048;
	//const double ALPF[]={ sqrt2,  sqrt2};
	//const double AHPF[]={-sqrt2,  sqrt2};
	//const double SLPF[]={ sqrt2,  sqrt2};
	//const double SHPF[]={ sqrt2, -sqrt2};

	//db2
	//const double ALPF[]={-0.129409522551260370, 0.224143868042013390, 0.836516303737807940, 0.482962913144534160};
	//const double AHPF[]={-0.482962913144534160, 0.836516303737807940, -0.224143868042013390, -0.129409522551260370};
	//const double SLPF[]={ 0.482962913144534160, 0.836516303737807940, 0.224143868042013390, -0.129409522551260370};
	//const double SHPF[]={-0.129409522551260370, -0.224143868042013390, 0.836516303737807940, -0.482962913144534160};

	//db3
	const double ALPF[]={0.035226291885709533, -0.085441273882026658, -0.135011020010254580, 0.459877502118491540, 0.806891509311092550, 0.332670552950082630};
	const double AHPF[]={-0.332670552950082630, 0.806891509311092550, -0.459877502118491540, -0.135011020010254580, 0.085441273882026658, 0.035226291885709533};
	const double SLPF[]={0.332670552950082630, 0.806891509311092550, 0.459877502118491540, -0.135011020010254580, -0.085441273882026658, 0.035226291885709533};
	const double SHPF[]={0.035226291885709533, 0.085441273882026658, -0.135011020010254580, -0.459877502118491540, 0.806891509311092550, -0.332670552950082630};

	//const double ALPF[]={0, -0.125, 0.25, 0.75, 0.25, -0.125, 0};
	//const double AHPF[]={0, -0.5, 1, -0.5, 0, 0, 0};
	//const double SLPF[]={0, 0, 0.5, 1, 0.5, 0, 0};
	//const double SHPF[]={0, 0, -0.125, -0.25, 0.75, -0.25, -0.125};

//	const double ALPF[]={0, 0.026748757410810, -0.016864118442875, -0.078223266528990, 0.266864118442875, 0.602949018236360, 0.266864118442875, -0.078223266528990, -0.016864118442875, 0.026748757410810, 0};
//	const double AHPF[]={0, 0.091271763114250, -0.057543526228500, -0.591271763114250, 1.115087052457000, -0.591271763114250, -0.057543526228500, 0.091271763114250, 0, 0, 0};
//	//const double AHPF[]={0, 0, 0.091271763114250, -0.057543526228500, -0.591271763114250, 1.115087052457000, -0.591271763114250, -0.057543526228500, 0.091271763114250, 0, 0};
//	const double SLPF[]={0, 0, -0.091271763114250, -0.057543526228500, 0.591271763114250, 1.115087052457000, 0.591271763114250, -0.057543526228500, -0.091271763114250, 0, 0};
//	const double SHPF[]={0, 0, 0.026748757410810, 0.016864118442875, -0.078223266528990, -0.266864118442875, 0.602949018236360, -0.266864118442875, -0.078223266528990, 0.016864118442875, 0.026748757410810};
//	//const double SHPF[]={0, 0.026748757410810, 0.016864118442875, -0.078223266528990, -0.266864118442875, 0.602949018236360, -0.266864118442875, -0.078223266528990, 0.016864118442875, 0.026748757410810, 0};
	
	double t0[width+4], t1[width+4];
	for(int k=0;k<width;++k)
		t0[k]=src[k];
	
	double matrix[(width+4)*(width+4)];
	wavelib_dwt_sym_stride_matrix(t0, width, ALPF, AHPF, sizeof(ALPF)/sizeof(double), t1, (width>>1)+2, t1+(width>>1)+2, 1, 1, matrix);
	printf("Forward DWT:\n");
	print_matrix(matrix, width, width+4);
	wavelib_idwt_sym_stride_matrix(t1, width>>1, t1+(width>>1)+2, SLPF, SHPF, sizeof(SLPF)/sizeof(double), t0, 1, 1, matrix);
	printf("Inverse DWT:\n");
	print_matrix(matrix, width, width);

	//double matrix[width*width];
	//dwt_1d_matrix(t0, t1, 1, width, ALPF, AHPF, sizeof(ALPF)/sizeof(double), matrix);
	//printf("Forward DWT:\n");
	//print_subimage_double(matrix, width, width, 0, 0, width, width);
	//idwt_1d_matrix(t0, t1, 1, width, SLPF, SHPF, sizeof(SLPF)/sizeof(double), matrix);
	//printf("Inverse DWT:\n");
	//print_subimage_double(matrix, width, width, 0, 0, width, width);

	//dwt_1d(t0, t1, 1, width, ALPF, AHPF, sizeof(ALPF)/sizeof(double));
	//dwt_1d(t0, t1, 1, width>>1, ALPF, AHPF, sizeof(ALPF)/sizeof(double));
	//idwt_1d(t0, t1, 1, width>>1, SLPF, SHPF, sizeof(SLPF)/sizeof(double));
	//idwt_1d(t0, t1, 1, width, SLPF, SHPF, sizeof(SLPF)/sizeof(double));//*/

/*	double *b0=nullptr;
	dwt_2d(src, false, width, height, ALPF, AHPF, sizeof(ALPF)/sizeof(double), true, &b0);
	print_subimage_double(b0, width, height, 0, 0, width, height, 5, 1);
	double *b1=nullptr;
	dwt_2d(b0, true, width, height, SLPF, SHPF, sizeof(SLPF)/sizeof(double), false, &b1);
	print_subimage_double(b1, width, height, 0, 0, width, height, 5, 1);
	int imsize=width*height;
	short *s2=new short[imsize*3];
	memcpy(s2, src, imsize<<1);
	for(int k=0;k<imsize;++k)
		s2[imsize+k]=(short)floor(b0[k]+0.5);
	for(int k=0;k<imsize;++k)
		s2[(imsize<<1)+k]=(short)floor(b1[k]+0.5);
	delete[] b0, b1;
	set_image(s2, width, height*3, depth, IM_GRAYSCALE);
	delete[] s2;//*/
#endif
#if 0
	const double ALPF[]=
	{
		0.602949018236360,
		0.266864118442875,
		-0.078223266528990,
		-0.016864118442875,
		0.026748757410810,
	};
	const double AHPF[]=
	{
		1.115087052457000,
		-0.591271763114250,
		-0.057543526228500,
		0.091271763114250,
		0,
	};
	const double SLPF[]=
	{
		1.115087052457000,
		0.591271763114250,
		-0.057543526228500,
		-0.091271763114250,
		0,
	};
	const double SHPF[]=
	{
		0.602949018236360,
		-0.266864118442875,
		-0.078223266528990,
		0.016864118442875,
		0.026748757410810,
	};
	//double *d0=nullptr;
	//dwt_2d(src, width, height, ALPF, AHPF, sizeof(ALPF)/sizeof(double), &d0);
	//print_subimage_double(d0, width, height, 0, 0, width, height, 5, 1);

	double t0[16], t1[16];
	for(int k=0;k<16;++k)
		t0[k]=src[k];
	dwt_1d(t0, t1, 1, 16, ALPF, AHPF, 5);
	dwt_1d(t0, t1, 1,  8, ALPF, AHPF, 5);
	dwt_1d(t0, t1, 1,  4, ALPF, AHPF, 5);
	dwt_1d(t0, t1, 1,  2, ALPF, AHPF, 5);

	//double *d0=nullptr;
	//dwt_1d(src, width, &d0);
	//print_subimage_double(d0, width, 1, 0, 0, width, 1, 5, 1);
#endif
#if 0
	int imsize=width*height, nlevels=1<<depth;
	int *histogram=new int[nlevels];
	calculate_histogram(src, imsize, histogram, nlevels);
	std::vector<int> palette(nlevels);
	for(int k=0;k<nlevels;++k)
		palette[k]=k;
	std::sort(palette.begin(), palette.end(), [&](int idx1, int idx2){return histogram[idx1]>histogram[idx2];});//descending order
	printf("Sorted histogram:\n");
	print_histogram(histogram, nlevels, imsize, palette.data());//
	short *dst=new short[imsize<<1];

	int w2=width, h2=height;
	memcpy(dst, src, imsize<<1);//copy image
	memset(dst+imsize, 0, imsize<<1);
	short *d1=dst, *d2=dst+w2*h2;
	printf("iteration 0:\n");
	print_subimage(d1, w2, h2, 0, 0, w2, h2, 4);
	for(int it=0;w2>=1&&h2>=1;it+=2)
	{
		for(int ky=0;ky<h2;++ky)//half width
		{
			for(int kx=0;kx<w2;kx+=2)
			{
				auto &v0=d1[w2*ky+kx], &v1=d1[w2*ky+kx+1];
				int minval=(v0+v1-abs(v0-v1))>>1;
				v0-=minval;
				v1-=minval;
				d2[(w2>>1)*ky+(kx>>1)]=minval;
			}
		}
		printf("iteration %d:\n", it+1);
		print_subimage(d1, w2, h2, 0, 0, w2, h2, 4);
		d1=d2;
		w2>>=1;
		d2+=w2*h2;
		print_subimage(d1, w2, h2, 0, 0, w2, h2, 4);

		for(int ky=0;ky<h2;ky+=2)//half height
		{
			for(int kx=0;kx<w2;++kx)
			{
				auto &v0=d1[w2*ky+kx], &v1=d1[w2*(ky+1)+kx];
				int minval=(v0+v1-abs(v0-v1))>>1;
				v0-=minval;
				v1-=minval;
				d2[w2*(ky>>1)+kx]=minval;
			}
		}
		printf("iteration %d:\n", it+2);
		print_subimage(d1, w2, h2, 0, 0, w2, h2, 4);
		d1=d2;
		h2>>=1;
		d2+=w2*h2;
		print_subimage(d1, w2, h2, 0, 0, w2, h2, 4);
	}
	//for(int ky=0;ky<height;ky+=4)
	//{
	//	for(int kx=0;kx<width;kx+=4)
	//	{
	//		int xmin=0, ymin=0;
	//		for(int ky2=0;ky2<4;++ky2)
	//			for(int kx2=0;kx2<4;++kx2)
	//				if(src[width*(ky+ymin)+kx+xmin]>src[width*(ky+ky2)+kx+kx2])
	//					xmin=kx2, ymin=ky2;
	//		int minval=src[width*(ky+ymin)+kx+xmin];
	//		for(int ky2=0;ky2<4;++ky2)
	//		{
	//			for(int kx2=0;kx2<4;++kx2)
	//			{
	//				if(ky2==ymin&&kx2==xmin)
	//					continue;
	//				src[width*(ky+ky2)+kx+kx2]-=minval;
	//			}
	//		}
	//	}
	//}
	//print_subimage(src, width, height, 0, 0, width>>1, height>>1, 2);
	//calculate_histogram(src, imsize, histogram, nlevels);

	calculate_histogram(dst, imsize<<1, histogram, nlevels);
	for(int k=0;k<nlevels;++k)
		palette[k]=k;
	std::sort(palette.begin(), palette.end(), [&](int idx1, int idx2){return histogram[idx1]>histogram[idx2];});
	printf("Sorted histogram:\n");
	print_histogram(histogram, nlevels, imsize<<1, palette.data());//

	//std::vector<int> data;
	//int data_idx=huff::compress_v5(src, width, height, depth, 'G'|'R'<<8|'B'<<16|'G', data);
	//printf("Uncompressed: %d bits\n", width*height*depth);
	//printf("RVL: %d bits\n", (data.size()-data_idx)<<5);


/*	int imsize=width*height, nlevels=1<<depth;
	int *histogram=new int[nlevels];
	calculate_histogram(src, imsize, histogram, nlevels);
	printf("Unsorted histogram:\n");
	print_histogram(histogram, nlevels, imsize, nullptr);//

	std::vector<int> palette(nlevels), invP(nlevels);
	for(int k=0;k<nlevels;++k)
		palette[k]=k;
	std::sort(palette.begin(), palette.end(), [&](int idx1, int idx2){return histogram[idx1]>histogram[idx2];});//descending order
	for(int k=0;k<nlevels;++k)
		invP[palette[k]]=k;
	short *dst=new short[imsize];
	for(int k=0;k<imsize;++k)
		dst[k]=invP[src[k]];

	printf("Palette-substituted image:\n");
	print_subimage(dst, width, height, 0, 0, width, height, 2);

	printf("Sorted histogram:\n");
	print_histogram(histogram, nlevels, imsize, palette.data());////*/
#endif

	set_image(dst, width, height, depth, IM_GRAYSCALE);
	console_pause();
	console_end();
	delete[] histogram;
#ifdef HEAP_SRC
	delete[] src;
#endif
	delete[] dst;
}

void			print_hex(const byte *buffer, int bytesize)
{
	for(int k=0;k<bytesize;++k)
		printf("%02X-", (unsigned)buffer[k]);
	printf("\n");
}
void			print_hex(const unsigned *buffer, int count)
{
	for(int k=0;k<count;++k)
		printf("%08X-", buffer[k]);
	printf("\n");
}

const int ac_logwindowbits=5,
	ac_windowbits=1<<ac_logwindowbits,
	ac_windowbitsmask=ac_windowbits-1,
	ac_den=ac_windowbits+2,
	ac_invden=(1<<16)/ac_den;
void			encode_arithmetic(const short *buffer, int imsize, int depth, std::vector<int> &data)
{
	std::vector<std::vector<int>> planes(depth);
	for(int kp=0;kp<depth;++kp)
	{
		auto &plane=planes[kp];

		int history[ac_windowbits];
		for(int k=0;k<ac_windowbits;++k)
			history[k]=k&1;
		int num=(ac_windowbits>>1)+1;
		for(int kb=0;kb<imsize;)
		{
			long long start=0, end=0x100000000;
			for(;kb<imsize;)
			{
				long long middle=start+((end-start)*(ac_den-num)*ac_invden>>16);
				if(end-middle<=1||middle-start<=1)
					break;

				int bit=buffer[kb]>>kp&1;
				if(bit)
					start=middle;
				else
					end=middle;

				int &sieve=history[kb&ac_windowbitsmask];
				num+=bit-sieve;
				sieve=bit;

				++kb;
				//if(kp==2&&kb==60)
				//	int LOL_1=0;
			}
			plane.push_back((unsigned)start);
		}
	}
	data.clear();
	for(int k=0, size=depth;k<depth;++k)
	{
		size+=planes[k].size();
		data.push_back(size);
	}
	for(int k=0;k<depth;++k)
	{
		auto &plane=planes[k];
		data.insert(data.end(), plane.begin(), plane.end());
	}
}
void			decode_arithmetic(int *data, short *buffer, int imsize, int depth)
{
	memset(buffer, 0, imsize<<1);
	
	for(int kp=0;kp<depth;++kp)
	{
		int ncodes=data[kp];
		auto plane=data+(kp?data[kp-1]:depth);

		int history[ac_windowbits];
		for(int k=0;k<ac_windowbits;++k)
			history[k]=k&1;
		int num=(ac_windowbits>>1)+1;
		for(int kc=0, kb=0;kc<ncodes&&kb<imsize;++kc)
		{
			unsigned code=plane[kc];
			long long start=0, end=0x100000000;
			for(;kb<imsize;)
			{
				long long middle=start+((end-start)*(ac_den-num)*ac_invden>>16);
				if(end-middle<=1||middle-start<=1)
					break;

				int bit=code>=middle;
				if(bit)
					start=middle;
				else
					end=middle;

				int &sieve=history[kb&ac_windowbitsmask];
				num+=bit-sieve;
				sieve=bit;

				buffer[kb]|=bit<<kp;
				++kb;
				//if(kp==2&&kb==60)
				//	int LOL_1=0;
			}
		}
	}
}

inline void		integerDCT8x8_step(__m128i *data)
{
	//https://stackoverflow.com/questions/18621167/dct-using-integer-only
	//https://fgiesen.wordpress.com/2013/11/04/bink-2-2-integer-dct-design-part-1/
	//stage 1
	__m128i a[8];
	a[0]=_mm_add_epi16(data[0], data[7]);
	a[1]=_mm_add_epi16(data[1], data[6]);
	a[2]=_mm_add_epi16(data[2], data[5]);
	a[3]=_mm_add_epi16(data[3], data[4]);
	a[4]=_mm_sub_epi16(data[0], data[7]);
	a[5]=_mm_sub_epi16(data[1], data[6]);
	a[6]=_mm_sub_epi16(data[2], data[5]);
	a[7]=_mm_sub_epi16(data[3], data[4]);
	
	//even stage 2
	__m128i b[8];
	b[0]=_mm_add_epi16(a[0], a[3]);
	b[1]=_mm_add_epi16(a[1], a[2]);
	b[2]=_mm_sub_epi16(a[0], a[3]);
	b[3]=_mm_sub_epi16(a[1], a[2]);
	
	//even stage 3
	__m128i c[8];
	c[0]=_mm_add_epi16(b[0], b[1]);
	c[1]=_mm_sub_epi16(b[0], b[1]);
	c[2]=_mm_add_epi16(_mm_add_epi16(b[2], _mm_srai_epi16(b[2], 2)), _mm_srai_epi16(b[3], 1));
	c[3]=_mm_sub_epi16(_mm_sub_epi16(_mm_srai_epi16(b[2], 1), b[3]), _mm_srai_epi16(b[3], 2));

	//odd stage 2
	__m128i a4_4=_mm_srai_epi16(a[4], 2), a7_4=_mm_srai_epi16(a[7], 2);
	b[4]=_mm_add_epi16(_mm_add_epi16(a7_4, a[4]), _mm_sub_epi16(a4_4, _mm_srai_epi16(a[4], 4)));
	b[7]=_mm_add_epi16(_mm_sub_epi16(a4_4, a[7]), _mm_sub_epi16(_mm_srai_epi16(a[7], 4), a7_4));
	b[5]=_mm_sub_epi16(_mm_add_epi16(a[5], a[6]), _mm_add_epi16(_mm_srai_epi16(a[6], 2), _mm_srai_epi16(a[6], 4)));
	b[6]=_mm_add_epi16(_mm_sub_epi16(a[6], a[5]), _mm_add_epi16(_mm_srai_epi16(a[5], 2), _mm_srai_epi16(a[5], 4)));

	//odd stage 3
	c[4]=_mm_add_epi16(b[4], b[5]);
	c[5]=_mm_sub_epi16(b[4], b[5]);
	c[6]=_mm_add_epi16(b[6], b[7]);
	c[7]=_mm_sub_epi16(b[6], b[7]);

	//stage 4
	data[0]=c[0];
	data[1]=c[4];
	data[2]=c[2];
	data[3]=_mm_sub_epi16(c[5], c[7]);
	data[4]=c[1];
	data[5]=_mm_add_epi16(c[5], c[7]);
	data[6]=c[3];
	data[7]=c[6];
	
	//odd stage 4
	//__m128i d4, d5, d6, d7;
	//d4=c[4];
	//d5=_mm_add_epi16(c[5], c[7]);
	//d6=_mm_sub_epi16(c[5], c[7]);
	//d7=c[6];
	//
	//permute/output
	//o[0]=c[0], o[1]=d4, o[2]=c[2], o[3]=d6, o[4]=c[1], o[5]=d5, o[6]=c[3], o[7]=d7;
}
inline void		transpose8x8(__m128i *data)
{
	//https://stackoverflow.com/questions/2517584/transpose-for-8-registers-of-16-bit-elements-on-sse2-ssse3
	__m128i a[8], b[8];
	a[0]=_mm_unpacklo_epi16(data[0], data[1]);
	a[1]=_mm_unpacklo_epi16(data[2], data[3]);
	a[2]=_mm_unpacklo_epi16(data[4], data[5]);
	a[3]=_mm_unpacklo_epi16(data[6], data[7]);
	a[4]=_mm_unpackhi_epi16(data[0], data[1]);
	a[5]=_mm_unpackhi_epi16(data[2], data[3]);
	a[6]=_mm_unpackhi_epi16(data[4], data[5]);
	a[7]=_mm_unpackhi_epi16(data[6], data[7]);

	b[0]=_mm_unpacklo_epi32(a[0], a[1]);
	b[1]=_mm_unpackhi_epi32(a[0], a[1]);
	b[2]=_mm_unpacklo_epi32(a[2], a[3]);
	b[3]=_mm_unpackhi_epi32(a[2], a[3]);
	b[4]=_mm_unpacklo_epi32(a[4], a[5]);
	b[5]=_mm_unpackhi_epi32(a[4], a[5]);
	b[6]=_mm_unpacklo_epi32(a[6], a[7]);
	b[7]=_mm_unpackhi_epi32(a[6], a[7]);

	data[0]=_mm_unpacklo_epi64(b[0], b[2]);
	data[1]=_mm_unpackhi_epi64(b[0], b[2]);
	data[2]=_mm_unpacklo_epi64(b[1], b[3]);
	data[3]=_mm_unpackhi_epi64(b[1], b[3]);
	data[4]=_mm_unpacklo_epi64(b[4], b[6]);
	data[5]=_mm_unpackhi_epi64(b[4], b[6]);
	data[6]=_mm_unpacklo_epi64(b[5], b[7]);
	data[7]=_mm_unpackhi_epi64(b[5], b[7]);
}
inline void		integerDCT8x8(short *buffer, int bw, int bh, int x, int y)
{
	__m128i data[8];
	for(int k=0, k2=bw*y;k<8;++k, k2+=bw)
		data[k]=_mm_loadu_si128((__m128i*)(buffer+k2));

	integerDCT8x8_step(data);
	transpose8x8(data);
	integerDCT8x8_step(data);
	transpose8x8(data);
	//for(int k=0;k<8;++k)//divide by 8
	//	data[k]=_mm_srli_epi16(data[k], 3);

	for(int k=0, k2=bw*y;k<8;++k, k2+=bw)
		_mm_storeu_si128((__m128i*)(buffer+k2), data[k]);

	//short in[64];
	//for(int k=0, k2=bw*y;k<8;++k, k2+=bw)
	//	memcpy(in+(k<<3), buffer+k2, 8*sizeof(short));

	//short i[8];
	//for(int k=0, k2=0;k<8;++k, k2+=stride)
	//	i[k]=buffer[k2];
}

void			dwt2_1d_scale(float *even, float *odd, int halfsize, float ce, float co)
{
	int xrem=halfsize&3, xround=halfsize-xrem;
	__m128 factor=_mm_set1_ps(ce);
	for(int k=0;k<xround;k+=4)
	{
		__m128 val=_mm_loadu_ps(even+k);
		val=_mm_mul_ps(val, factor);
		_mm_storeu_ps(even+k, val);
	}
	for(int k=xround;k<halfsize;++k)
		even[k]*=ce;
	factor=_mm_set1_ps(co);
	for(int k=0;k<xround;k+=4)
	{
		__m128 val=_mm_loadu_ps(odd+k);
		val=_mm_mul_ps(val, factor);
		_mm_storeu_ps(odd+k, val);
	}
	for(int k=xround;k<halfsize;++k)
		odd[k]*=co;
}
void			dwt2_1d_predict_next(float *even, float *odd, int halfsize, float z10)
{
	--halfsize;
	int xround=halfsize-(halfsize&3);
	__m128 f0=_mm_set1_ps(z10);
	for(int k=0;k<xround;k+=4)
	{
		__m128 vo=_mm_loadu_ps(odd+k);
		__m128 ve0=_mm_loadu_ps(even+k);
		__m128 ve1=_mm_loadu_ps(even+k+1);
		ve0=_mm_add_ps(ve0, ve1);
		ve0=_mm_mul_ps(ve0, f0);
		ve0=_mm_add_ps(ve0, vo);
		_mm_storeu_ps(odd+k, ve0);
	}
	for(int k=xround;k<halfsize;++k)
		odd[k]+=z10*(even[k]+even[k+1]);
	odd[halfsize]+=z10*(even[halfsize]+even[halfsize-1]);//symmetric padding at boundary
}
void			dwt2_1d_update_prev(float *even, float *odd, int halfsize, float z0m1)
{
	--halfsize;
	__m128 f0=_mm_set1_ps(z0m1);
	int k=halfsize-4;
	for(;k>0;k-=4)
	{
		__m128 ve=_mm_loadu_ps(even+k);
		__m128 vo0=_mm_loadu_ps(odd+k);
		__m128 vo1=_mm_loadu_ps(odd+k-1);
		vo0=_mm_add_ps(vo0, vo1);
		vo0=_mm_mul_ps(vo0, f0);
		vo0=_mm_add_ps(vo0, ve);
		_mm_storeu_ps(even+k, vo0);
	}
	k+=4;
	for(;k>0;--k)
		even[k]+=z0m1*(odd[k]+odd[k-1]);
	even[0]+=z0m1*(odd[0]+odd[1]);
#if 0//X	should go backwards
	--halfsize;
	int xround=halfsize-(halfsize&3);
	even[0]+=z0m1*(odd[0]+odd[1]);//symmetric padding at boundary
	__m128 f0=_mm_set1_ps(z0m1);
	for(int k=1;k<xround+1;k+=4)
	{
		__m128 ve=_mm_loadu_ps(even+k);
		__m128 vo0=_mm_loadu_ps(odd+k);
		__m128 vo1=_mm_loadu_ps(odd+k-1);
		vo0=_mm_add_ps(vo0, vo1);
		vo0=_mm_mul_ps(vo0, f0);
		vo0=_mm_add_ps(vo0, ve);
		_mm_storeu_ps(even+k, vo0);
	}
	for(int k=xround;k<halfsize;++k)
		even[k]+=z0m1*(odd[k]+odd[k-1]);
#endif
}
//void			dwt2_1d_d4(float *buffer, int halfsize)
//{
//	//'factring wavelet transforms into lifting steps' - page 16
//	float coeff=-sqrt(3.f);
//}
void			dwt2_1d_9_7(float *even, float *odd, int halfsize)
{
	//'factring wavelet transforms into lifting steps' - page 19
	//'a wavelet tour of signal processing - the sparse way' - page 376
	const double alpha=-1.58613434342059, beta=-0.0529801185729, gamma=0.8829110755309, delta=0.4435068520439, zeta=1.1496043988602;
	dwt2_1d_scale(even, odd, halfsize, zeta, 1/zeta);
	dwt2_1d_predict_next(even, odd, halfsize, delta);
	dwt2_1d_update_prev(even, odd, halfsize, gamma);
	dwt2_1d_predict_next(even, odd, halfsize, beta);
	dwt2_1d_update_prev(even, odd, halfsize, alpha);
}
void			dwt2_1d_9_7_inv(float *even, float *odd, int halfsize)
{
	const double alpha=-1.58613434342059, beta=-0.0529801185729, gamma=0.8829110755309, delta=0.4435068520439, zeta=1.1496043988602;
	dwt2_1d_update_prev(even, odd, halfsize, -alpha);
	dwt2_1d_predict_next(even, odd, halfsize, -beta);
	dwt2_1d_update_prev(even, odd, halfsize, -gamma);
	dwt2_1d_predict_next(even, odd, halfsize, -delta);
	dwt2_1d_scale(even, odd, halfsize, 1/zeta, zeta);
}
void			dwt2_1d(float *buffer, int size, int stride, float *b2)//size is even
{
	int halfsize=size>>1;
	float *even=b2+halfsize, *odd=b2;
	
	for(int k=0, ks=0;k<halfsize;++k, ks+=stride<<1)//lazy wavelet: split into even & odd
	{
		even[k]=buffer[ks];
		odd[k]=buffer[ks+stride];
	}

	dwt2_1d_9_7(even, odd, halfsize);

	for(int k=0, ks=0;k<size;++k, ks+=stride)
		buffer[ks]=b2[k];
#if 0
	if(!inv)
	{
		for(int k=0, ks=0;k<halfsize;++k, ks+=stride<<1)//lazy wavelet: split into even & odd
		{
			even[k]=buffer[ks];
			odd[k]=buffer[ks+stride];
		}
		dwt2_1d_9_7(even, odd, halfsize);
		for(int k=0, ks=0;k<size;++k, ks+=stride)
			buffer[ks]=b2[k];
	}
	else
	{
		for(int k=0, ks=0;k<size;++k, ks+=stride)
			b2[k]=buffer[ks];
		dwt2_1d_9_7_inv(even, odd, halfsize);
		for(int k=0, ks=0;k<halfsize;++k, ks+=stride<<1)//inv lazy wavelet: join even & odd
		{
			buffer[ks]=even[k];
			buffer[ks+stride]=odd[k];
		}
	}
#endif
#if 0//size divisible by 8, temp: size + padsize * 4
	const int padsize=1;//on both ends
	int halfsize=size>>1;
	float *even=b2+padsize, *odd=b2+halfsize+padsize*3;
	for(int k=0, ks=0;k<size;k+=2, ks+=stride<<1)//lazy wavelet: split into even & odd
	{
		even[k>>1]=buffer[ks];
		odd[k>>1]=buffer[ks+stride];
	}
	even[-1]=even[1];//symmetric padding
	even[halfsize]=even[halfsize-2];
	odd[-1]=odd[1];
	odd[halfsize]=odd[halfsize-2];

	dwt2_1d_9_7(b2, halfsize);
	//dwt2_1d_d4(b2, halfsize);

	for(int k=0, ks=0;k<size;k+=2, ks+=stride<<1)//inv lazy wavelet: join even & odd
	{
		buffer[ks]=even[k>>1];
		buffer[ks+stride]=odd[k>>1];
	}
#endif
}
void			dwt2_1d_inv(float *buffer, int size, int stride, float *b2)//size is even
{
	int halfsize=size>>1;
	float *even=b2+halfsize, *odd=b2;

	for(int k=0, ks=0;k<size;++k, ks+=stride)
		b2[k]=buffer[ks];

	dwt2_1d_9_7_inv(even, odd, halfsize);

	for(int k=0, ks=0;k<halfsize;++k, ks+=stride<<1)//inv lazy wavelet: join even & odd
	{
		buffer[ks]=even[k];
		buffer[ks+stride]=odd[k];
	}
}
void			dwt2_2d(float *buffer, int bw, int bh, int nstages)
{
	int tsize=maximum(bw, bh);
	float *temp=new float[tsize];
	for(int w2=bw, h2=bh, it=0;w2>=3&&h2>=3&&(!nstages||it<nstages);++it)
	{
		for(int ky=0;ky<h2;++ky)//horizontal DWT
			dwt2_1d(buffer+bw*ky, w2, 1, temp);

		for(int kx=0;kx<w2;++kx)//vertical DWT
			dwt2_1d(buffer+kx, h2, bw, temp);

		w2-=w2>>1;//w=ceil(w/2)
		h2-=h2>>1;//h=ceil(h/2)
	}
	delete[] temp;
}
void			dwt2_2d_inv(float *buffer, int bw, int bh, int nstages)
{
	int lw=floor_log2(bw), lh=floor_log2(bh);
	short *sizes=new short[minimum(lw, lh)<<1];
	int nsizes=0;
	for(int w2=bw, h2=bh;w2>=3&&h2>=3&&(!nstages||nsizes<nstages);++nsizes)//calculate dimensions of each stage
	{
		sizes[nsizes<<1]=w2;
		sizes[(nsizes<<1)+1]=h2;
		w2-=w2>>1;//w=ceil(w/2)
		h2-=h2>>1;//h=ceil(h/2)
	}

	int tsize=maximum(bw, bh);
	float *temp=new float[tsize];
	for(int it=nsizes-1;it>=0;--it)
	{
		int w2=sizes[it<<1], h2=sizes[(it<<1)+1];

		for(int kx=0;kx<w2;++kx)//vertical IDWT
			dwt2_1d_inv(buffer+kx, h2, bw, temp);

		for(int ky=0;ky<h2;++ky)//horizontal IDWT
			dwt2_1d_inv(buffer+bw*ky, w2, 1, temp);
	}
	delete[] sizes, temp;
}

void			print_subimage(float *buffer, int bw, int bh, int x0, int y0, int dx, int dy, int chars, int decimals)
{
	for(int ky=0;ky<dy;++ky)
	{
		for(int kx=0;kx<dx;++kx)
			printf("%*.*f  ", chars, decimals, buffer[bw*(y0+ky)+x0+kx]);
			//printf("%10.5f  ", buffer[bw*(y0+ky)+x0+kx]);
		printf("\n");
	}
	printf("\n");
}

int				count_pot(int x)
{
	for(int k=0;k<32;++k)
		if(x>>k&1)
			return k;
	return -1;
}
void			archiver_test4()
{
	//console_start_good();
#if 1
	static bool temp=false;
	int pw=count_pot(iw), ph=count_pot(ih);
	int pot_count=minimum(pw, ph);
	if(temp=!temp)
		dwt2_2d(image, iw, ih, pot_count);
	else
		dwt2_2d_inv(image, iw, ih, pot_count);
	render();
#endif
#if 0
	int bw=16, bh=16;
	float src[]=
	{
		855,  820,  817,  797,  825,  828,  843,  832,  828,  892, 2088, 2211, 2218, 2220, 2209, 2146,
		830,  832,  830,  813,  846,  805,  820,  844,  843,  956, 2222, 2268, 2179, 2164, 2269, 2207,
		829,  784,  808,  824,  837,  790,  828,  840,  828, 1016, 2269, 2255, 2163, 2225, 2297, 2147,
		802,  833,  813,  836,  817,  835,  814,  828,  796, 1193, 2289, 2195, 2215, 2278, 2262, 2211,
		793,  821,  854,  860,  836,  799,  839,  830,  845, 1413, 2273, 2238, 2226, 2288, 2291, 2346,
		805,  835,  850,  829,  821,  832,  805,  812,  812, 1500, 2272, 2164, 2228, 2302, 2387, 2534,
		827,  838,  849,  792,  805,  831,  833,  831,  805, 1643, 2335, 2249, 2291, 2295, 2472, 2618,
		811,  841,  833,  836,  820,  839,  812,  838,  807, 1783, 2307, 2296, 2347, 2315, 2522, 2604,
		805,  843,  816,  823,  810,  801,  846,  840,  827, 1955, 2325, 2322, 2312, 2407, 2516, 2577,
		826,  818,  784,  828,  825,  820,  823,  840,  838, 2055, 2300, 2368, 2364, 2417, 2567, 2532,
		829,  803,  807,  794,  809,  806,  837,  837,  847, 2112, 2290, 2356, 2404, 2583, 2633, 2603,
		854,  834,  810,  822,  819,  833,  820,  844,  906, 2235, 2322, 2306, 2409, 2579, 2621, 2488,
		809,  816,  801,  810,  781,  840,  828,  832, 1071, 2319, 2366, 2302, 2422, 2573, 2515, 2508,
		810,  814,  821,  817,  815,  816,  831,  798, 1202, 2303, 2289, 2360, 2451, 2611, 2507, 2525,
		792,  840,  813,  829,  856,  839,  808,  777, 1396, 2339, 2334, 2406, 2436, 2595, 2497, 2499,
		815,  810,  800,  824,  821,  830,  839,  823, 1533, 2347, 2315, 2402, 2536, 2495, 2540, 2531,
		//1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 2, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1,
	};

	//int chars=10, decimals=5;
	int chars=5, decimals=2;
	//print_subimage(src, bw, bh, 0, 0, bw, 1, chars, decimals);
	//dwt2_1d(src, bw, 1, src+4);
	//print_subimage(src, bw, bh, 0, 0, bw, 1, chars, decimals);
	//dwt2_1d_inv(src, bw, 1, src+4);
	//print_subimage(src, bw, bh, 0, 0, bw, 1, chars, decimals);

	print_subimage(src, bw, bh, 0, 0, bw, bh, chars, decimals);
	dwt2_2d(src, bw, bh, 0);
	print_subimage(src, bw, bh, 0, 0, bw, bh, chars, decimals);
	dwt2_2d_inv(src, bw, bh, 0);
	print_subimage(src, bw, bh, 0, 0, bw, bh, chars, decimals);

	//dwt2_2d(image, iw, ih, 1);
	//dwt2_2d_inv(image, iw, ih, 1);
#endif
#if 0
#if 0
	if(!image)
	{
		printf("Please open an image first\n");
		console_pause();
		console_end();
		return;
	}
	int width=iw, height=ih;
	int depth=idepth;
	int imsize=width*height;
	short *src=get_image();
	short *dst=new short[imsize];
#define		HEAP_SRC2
#else
	const int width=8, height=8, depth=4;
	//const int width=16, height=16, depth=12;
	const int imsize=width*height;
	short src[imsize*3]=
	{
		//198, 200, 196, 196, 194, 186, 176, 159,
		//197, 197, 194, 200, 195, 181, 168, 147,
		//195, 192, 190, 201, 197, 179, 156, 138,
		//197, 192, 192, 202, 194, 170, 144, 135,
		//199, 195, 196, 200, 186, 155, 136, 138,
		//200, 197, 197, 199, 179, 144, 131, 138,
		//199, 197, 196, 197, 175, 140, 123, 132,
		//198, 193, 191, 197, 182, 143, 124, 122,
		//208, 203, 215, 208, 213, 202, 194, 192, 212, 211, 201, 202, 204, 202, 204, 215,
		//199, 208, 215, 205, 204, 201, 203, 196, 209, 212, 203, 202, 212, 207, 217, 214,
		//207, 226, 211, 202, 213, 218, 202, 205, 200, 205, 208, 213, 220, 217, 215, 215,
		//212, 200, 213, 209, 223, 214, 216, 209, 209, 215, 224, 213, 223, 227, 227, 221,
		//208, 214, 219, 204, 208, 206, 216, 216, 219, 222, 221, 226, 233, 229, 229, 218,
		//207, 214, 217, 209, 211, 215, 203, 208, 226, 231, 227, 231, 239, 236, 231, 226,
		//212, 206, 209, 218, 211, 214, 214, 224, 226, 233, 238, 238, 244, 244, 239, 232,
		//213, 212, 215, 230, 218, 216, 220, 224, 235, 238, 242, 252, 248, 252, 250, 237,
		//220, 217, 209, 218, 224, 212, 222, 227, 238, 241, 243, 249, 248, 251, 248, 241,
		//218, 223, 215, 209, 222, 214, 220, 227, 237, 237, 245, 252, 252, 248, 252, 241,
		//216, 217, 215, 220, 219, 226, 223, 226, 233, 236, 241, 245, 250, 252, 250, 240,
		//215, 215, 216, 210, 215, 222, 232, 233, 237, 233, 237, 236, 239, 234, 239, 225,
		//216, 219, 213, 214, 216, 227, 227, 223, 232, 234, 235, 237, 234, 234, 223, 228,
		//223, 221, 222, 216, 219, 224, 225, 225, 231, 236, 239, 242, 232, 224, 222, 218,
		//212, 221, 219, 216, 216, 215, 220, 227, 228, 247, 250, 249, 241, 241, 231, 221,
		//225, 223, 217, 213, 214, 216, 209, 221, 230, 255, 252, 251, 250, 251, 236, 220,
		//54, 61, 64, 68, 56, 52, 57, 69, 65, 69, 64, 49, 66, 67, 62, 69,
		//76, 64, 66, 75, 66, 77, 79, 67, 67, 62, 65, 62, 62, 54, 79, 70,
		//62, 60, 70, 47, 62, 62, 57, 74, 60, 74, 65, 69, 70, 70, 60, 66,
		//74, 68, 42, 63, 72, 55, 73, 61, 73, 73, 66, 57, 67, 48, 56, 66,
		//70, 68, 75, 68, 69, 59, 76, 64, 63, 49, 75, 68, 57, 58, 77, 61,
		//64, 72, 58, 72, 57, 66, 59, 81, 67, 60, 64, 67, 66, 55, 66, 75,
		//80, 64, 60, 55, 66, 59, 69, 63, 72, 62, 62, 72, 73, 61, 83, 64,
		//63, 51, 72, 72, 57, 60, 83, 61, 62, 85, 67, 62, 55, 71, 60, 53,
		//67, 63, 61, 73, 65, 66, 63, 66, 73, 60, 63, 50, 77, 66, 59, 63,
		//69, 64, 78, 68, 60, 66, 72, 61, 75, 71, 66, 60, 56, 65, 66, 67,
		//58, 70, 69, 76, 60, 61, 66, 66, 66, 80, 66, 64, 57, 53, 71, 66,
		//74, 64, 53, 58, 75, 65, 79, 72, 63, 52, 66, 64, 71, 77, 68, 75,
		//69, 60, 76, 83, 64, 57, 71, 64, 64, 64, 66, 64, 73, 52, 62, 60,
		//64, 54, 69, 61, 59, 70, 61, 66, 65, 70, 70, 72, 70, 62, 59, 60,
		//78, 76, 63, 69, 74, 71, 61, 45, 75, 70, 70, 63, 69, 66, 67, 69,
		//66, 56, 68, 54, 75, 80, 80, 71, 66, 71, 71, 64, 62, 67, 67, 83,
		//  411,  442,  462,  457,  441,  446,  464,  505,  500,  581,  800, 1074, 1195, 1288, 1398, 1519,
		//  409,  443,  433,  431,  426,  431,  450,  544,  568,  652,  896, 1134, 1202, 1310, 1394, 1428,
		//  415,  434,  406,  412,  423,  445,  496,  544,  603,  806, 1072, 1286, 1398, 1482, 1463, 1372,
		//  410,  402,  392,  419,  443,  467,  479,  580,  668,  918, 1241, 1386, 1468, 1520, 1458, 1637,
		//  390,  392,  422,  447,  464,  494,  542,  624,  836, 1173, 1410, 1520, 1472, 1523, 1717, 1893,
		//  395,  382,  424,  452,  483,  504,  584,  676, 1151, 1693, 1873, 1808, 1715, 1831, 1938, 1919,
		//  393,  383,  400,  451,  507,  543,  617, 1090, 2421, 2964, 2653, 2119, 1835, 1848, 1973, 1984,
		//  390,  393,  407,  455,  513,  574,  675, 2234, 3593, 3359, 2592, 1942, 1768, 1936, 1959, 1990,
		//  398,  396,  420,  490,  530,  611,  810, 2101, 2796, 2359, 1881, 1735, 1757, 1802, 1872, 1900,
		//  417,  450,  478,  493,  544,  603,  836, 1301, 1568, 1765, 1676, 1701, 1724, 1760, 1700, 1681,
		//  424,  474,  492,  501,  530,  562,  777, 1129, 1418, 1541, 1592, 1746, 1769, 1728, 1637, 1598,
		//  409,  433,  474,  502,  502,  570,  802, 1203, 1428, 1544, 1557, 1723, 1781, 1692, 1614, 1569,
		//  408,  434,  443,  460,  497,  574,  888, 1250, 1417, 1527, 1608, 1614, 1691, 1626, 1563, 1534,
		//  432,  434,  467,  476,  509,  629,  940, 1214, 1283, 1405, 1580, 1597, 1582, 1523, 1490, 1550,
		//  481,  484,  495,  517,  574,  733,  970, 1154, 1189, 1289, 1456, 1487, 1508, 1541, 1553, 1615,
		//  485,  491,  487,  545,  564,  815, 1056, 1167, 1270, 1343, 1416, 1482, 1528, 1544, 1596, 1675,
		//0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		//1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	};
	short *dst=src+imsize;
#endif
#if 1
	//memset(src, 0, imsize*sizeof(short));
	for(int ky=0;ky<height;++ky)
	{
		for(int kx=0;kx<width;++kx)
		{
			int x=8-kx, y=ky+1;
			int mag=64-x*x-y*y;
			if(mag>=0)
				//src[width*ky+kx]=(int)floor(31*sqrt((double)mag));
				src[width*ky+kx]=(int)floor(sqrt((double)mag));
			else
				src[width*ky+kx]=0;

			//src[width*ky+kx]=(kx&1)^(ky&1);

			//src[width*ky+kx]=!kx&&!ky;
		}
	}
	memcpy(dst, src, imsize*sizeof(short));
	integerDCT8x8(dst, width, height, 0, 0);
#endif
#if 0
	std::vector<int> data;
	auto t1=time_ms();
	//huff::compress_v5(src, width, height, depth, 'G'|'R'<<8|'B'<<16|'G'<<24, data);//27.274033ms
	encode_arithmetic(src, imsize, depth, data);//308.837774ms
	printf("Encode: %lfms\n", time_ms()-t1);
	printf("Original size = %d bits\n", width*height*depth);
	printf("Encoded size = %d bits\n", data.size()<<5);
	t1=time_ms();
	//dst=nullptr;//decompress calls realloc
	//huff::decompress((byte*)data.data(), data.size()<<2, RF_I16_BAYER, (void**)&dst, width, height, depth, nullptr);//22.398665ms
	decode_arithmetic(data.data(), dst, imsize, depth);//255.240717ms
	printf("Decode: %lfms\n", time_ms()-t1);
	bool valid=true;
	for(int kp=0;kp<depth;++kp)
	{
		for(int k=0;k<imsize;++k)
		{
			if((src[k]>>kp&1)!=(dst[k]>>kp&1))
			{
				printf("Error bit %d at [%d](%d, %d):\n", kp, k, k%width, k/width);
				printf("\t0x%04X=%d =\t", src[k], src[k]);
				for(int kb=0;kb<16;++kb)
					printf("%d", src[k]>>(15-kb)&1);
				printf("\n");
				printf("\t0x%04X=%d =\t", dst[k], dst[k]);
				for(int kb=0;kb<16;++kb)
					printf("%d", dst[k]>>(15-kb)&1);
				printf("\n");
				
				//printf("Error bit %d at [%d](%d, %d):\n\t0x%04X=%d\n\t0x%04X=%d\n", kp, k, k%width, k/width, src[k], src[k], dst[k], dst[k]);
				//printf("Error at %d: %d=0x%X -> %d=0x%X\n", k, src[k], src[k], dst[k], dst[k]);
				valid=false;
				break;
			}
		}
	}
	if(valid)
		printf("Decoding successful\n");
	print_subimage(dst, width, height, 0, 0, 16, 16, 2);
#endif
#ifdef HEAP_SRC2
	set_image(dst, width, height, depth, imagetype);
	delete[] src;
	delete[] dst;
	//free(dst);
#else
	short *diff=dst+imsize;
	for(int k=0;k<imsize;++k)
		diff[k]=src[k]^dst[k];
	set_image(src, width, height*3, depth, IM_GRAYSCALE);
#endif
#endif
#if 0
	const byte msg[]={0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02};
	//const byte msg[]={0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x01, 0x20, 0x80};
	const int size=sizeof(msg);
	
	printf("Source:\n");
	print_hex(msg, size);

	std::vector<unsigned> data;
	int bitlen=size<<3;
	const int logwindowbits=5,
		windowbits=1<<logwindowbits,
		windowbitsmask=windowbits-1,
		den=windowbits+2,
		invden=(1<<16)/den;
	int num=(windowbits>>1)+1;
	int history[windowbits];
	for(int k=0;k<windowbits;++k)
		history[k]=k&1;
	//for(int k=0;k<(windowbits>>1);++k)
	//	history[k]=1;
	for(int kb=0;kb<bitlen;)
	{
		long long start=0, end=0x100000000;
		for(;kb<bitlen;)
		{
			int bit=msg[kb>>3]>>(kb&7)&1;
			long long middle=start+((end-start)*(den-num)*invden>>16);
			long long s0=start, e0=end;
			if(bit)
				start=middle;
			else
				end=middle;

			//printf("%d ", num);//
			int &sieve=history[kb&windowbitsmask];
			num+=bit-sieve;
			sieve=bit;

			//int hamming=0;
			//for(int k=0;k<windowbits;++k)//
			//	hamming+=history[k];//
			//if(hamming+1!=num||!num||num==windowbits)
			//	int LOL_1=0;

			++kb;
			if(e0-middle<=1||middle-s0<=1)
				break;
		}
		data.push_back((unsigned)start);
	}
	//printf("\n");//
	printf("Compressed data:\n");
	print_hex(data.data(), data.size());
	
	std::vector<byte> dst;
	num=(windowbits>>1)+1;
	for(int k=0;k<windowbits;++k)
		history[k]=k&1;
	//for(int k=0;k<(windowbits>>1);++k)
	//	history[k]=1;
	//for(int k=windowbits>>1;k<windowbits;++k)
	//	history[k]=0;
	for(int kc=0, kb=0;kc<(int)data.size()&&kb<bitlen;++kc)
	{
		unsigned code=data[kc];
		long long start=0, end=0x100000000;
		for(;kb<bitlen;)
		{
			long long middle=start+((end-start)*(den-num)*invden>>16);
			int bit=code>=middle;
			long long s0=start, e0=end;
			if(bit)
				start=middle;
			else
				end=middle;

			//printf("%d ", num);//
			int &sieve=history[kb&windowbitsmask];
			num+=bit-sieve;
			sieve=bit;

			if(kb>=(dst.size()<<3))
				dst.push_back(0);
			dst[kb>>3]|=bit<<(kb&7);
			++kb;
			if(e0-middle<=1||middle-s0<=1)
				break;
		}
	}
	printf("\n");//
	printf("Uncompressed data:\n");
	print_hex(dst.data(), dst.size());
	
/*	std::vector<unsigned> data;
	int bitlen=size<<3;
	int hamming=8;//TODO: calculate hamming weight
	int pzeronum=bitlen-hamming+1, pzeroden=bitlen+1;//P(0)=num/den, P(1)=(den-num)/den
	for(int kb=0;kb<bitlen;)
	{
		long long start=0, end=0x100000000;
		for(;kb<bitlen;)
		{
			int bit=msg[kb>>3]>>(kb&7)&1;
			++kb;
			long long middle=start+(end-start)*pzeronum/pzeroden;
			long long s0=start, e0=end;
			if(bit)
				start=middle;
			else
				end=middle;
			if(e0-middle<=1||middle-s0<=1)
				break;
		}
		data.push_back((unsigned)start);
	}
	printf("Compressed data:\n");
	print_hex(data.data(), data.size());

	std::vector<byte> dst;
	for(int kc=0, kb=0;kc<(int)data.size()&&kb<bitlen;++kc)
	{
		unsigned code=data[kc];
		long long start=0, end=0x100000000;
		for(;kb<bitlen;)
		{
			//if(kb==6*8+4)//
			//	int LOL_1=0;//
			long long middle=start+(end-start)*pzeronum/pzeroden;
			int bit=code>=middle;
			long long s0=start, e0=end;
			if(bit)
				start=middle;
			else
				end=middle;
			if(kb>=(dst.size()<<3))
				dst.push_back(0);
			dst[kb>>3]|=bit<<(kb&7);
			++kb;
			if(e0-middle<=1||middle-s0<=1)
				break;
		}
	}
	printf("Uncompressed data:\n");
	print_hex(dst.data(), dst.size());//*/
#endif
	//console_pause();
	//console_end();
}