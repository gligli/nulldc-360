#include "drkPvr.h"

#include <math.h>
#include <algorithm>
//#include "gl\gl.h"
#include "regs.h"
#include <vector>
#include "Renderer_if.h"
//#include <xmmintrin.h>
#include "threadedPvr.h"

#if REND_API == REND_D3D

#ifdef USE_GUI
extern "C" struct XenosDevice * GetVideoDevice();
#endif

#define MAX_VERTEX_COUNT 1024*1024

extern char inc_vs[];
extern char inc_ps[];
struct XenosDevice _xe, *xe = NULL;
struct XenosShader *sh_ps, *sh_vs;
struct XenosVertexBuffer *vb;

struct XenosSurface * rtt_texture[2]={0,0};
u32 rtt_index=0;
u32 rtt_address=-1;
u32 rtt_FrameNumber=0xFFFFFFFF;
u32 ppar_BackBufferWidth,ppar_BackBufferHeight;

struct XenosSurface * fb_texture=0;
struct XenosSurface * pal_texture=0;
struct XenosSurface * fog_texture=0;

bool gfx_do_resize=false;
bool gfx_do_restart=false;

const struct XenosVBFFormat VertexBufferFormat = {
    4, {
        {XE_USAGE_POSITION, 0, XE_TYPE_FLOAT3},
        {XE_USAGE_COLOR,    0, XE_TYPE_FLOAT4},
        {XE_USAGE_COLOR,    1, XE_TYPE_FLOAT4},
	    {XE_USAGE_TEXCOORD, 0, XE_TYPE_FLOAT2},
    }
};

bool hadTriangles=false;
bool syncPending=false;


#define MODVOL 1
#define _float_colors_
//#define _HW_INT_
//#include <D3dx9shader.h>

using namespace TASplitter;
bool render_restart = false;
bool UseSVP=false;
bool UseFixedFunction=false;
bool dosort=false;
#if _DEBUG
	#define SHADER_DEBUG D3DXSHADER_DEBUG|D3DXSHADER_SKIPOPTIMIZATION
#else
	#define SHADER_DEBUG 0 /*D3DXSHADER_DEBUG|D3DXSHADER_SKIPOPTIMIZATION*/
#endif
/*
#define DEV_CREATE_FLAGS D3DCREATE_HARDWARE_VERTEXPROCESSING
//#define DEV_CREATE_FLAGS D3DCREATE_SOFTWARE_VERTEXPROCESSING

#if DEV_CREATE_FLAGS==D3DCREATE_HARDWARE_VERTEXPROCESSING
	#define VB_CREATE_FLAGS 0
#else
	#define VB_CREATE_FLAGS D3DUSAGE_SOFTWAREPROCESSING
#endif
*/
#define scale_type_1

//Convert offset32 to offset64
u32 vramlock_ConvOffset32toOffset64(u32 offset32);

//these can be used to force a profile
#define FORCE_SW_VERTEX_SHADERS (0)
#define FORCE_FIXED_FUNCTION    (0)
//#define D3DXGetPixelShaderProfile(x) "ps_2_0"
//#define D3DXGetVertexShaderProfile(x) "vs_2_0"

#define PS_SHADER_COUNT (384*4)
	bool RenderWasStarted=false;
	struct VertexDecoder;
	FifoSplitter<VertexDecoder> TileAccel;

	struct { bool needs_resize;NDC_WINDOW_RECT new_size;u32 rev;} resizerq;

	u32 clear_rt=0;
	u32 last_ps_mode=0xFFFFFFFF;
	float current_scalef[4];
	//CRITICAL_SECTION tex_cache_cs;
	
	u32 FrameNumber=0;
	u32 fb_FrameNumber=0;

	u32 frameStart = 0;
	u32 frameRate = 0;
	u32 timer, timeStart = timeGetTime();

	wchar fps_text[512];
	float res_scale[4]={0,0,320,-240};
	float fb_scale[2]={1,1};

	//x=emulation mode
	//y=filter mode
	//result = {d3dmode,shader id}
	void HandleEvent(u32 evid,void* p)
	{
		if (evid == NDE_GUI_RESIZED)
		{
			resizerq.needs_resize=true;
			if (p)
				memcpy((void*)&resizerq.new_size,p,sizeof(NDC_WINDOW_RECT));
			resizerq.rev++;
		}
	}
	void SetRenderRect(float* rect,bool do_clear)
	{
		res_scale[0]=rect[0];
		res_scale[1]=rect[1];

		res_scale[2]=rect[2]/2;
		res_scale[3]=-rect[3]/2;

		if(do_clear)
			clear_rt|=1;
	}
	void SetFBScale(float x,float y)
	{
		fb_scale[0]=x;
		fb_scale[1]=y;
	}
	const static u32 CullMode[]= 
	{
		
		XE_CULL_NONE,	//0	No culling	no culling
		XE_CULL_NONE,	//1	Cull if Small	Cull if	( |det| < fpu_cull_val )

		//wtf ?
		XE_CULL_CCW /*D3DCULL_CCW*/,		//2	Cull if Negative	Cull if 	( |det| < 0 ) or
						//( |det| < fpu_cull_val )
		XE_CULL_CW /*D3DCULL_CW*/,		//3	Cull if Positive	Cull if 	( |det| > 0 ) or
						//( |det| < fpu_cull_val )
		
		
		XE_CULL_NONE,	//0	No culling	no culling
		XE_CULL_NONE,	//1	Cull if Small	Cull if	( |det| < fpu_cull_val )

		//wtf ?
		XE_CULL_CW /*D3DCULL_CCW*/,		//2	Cull if Negative	Cull if 	( |det| < 0 ) or
						//( |det| < fpu_cull_val )
		XE_CULL_CCW /*D3DCULL_CW*/,		//3	Cull if Positive	Cull if 	( |det| > 0 ) or
						//( |det| < fpu_cull_val )
		
	};
	const static u32 Zfunction[]=
	{
		//This bit is used in combination with the Z Write Disable bit, and 
		//supports compare processing, which is required for OpenGL and D3D 
		//versus Z buffer updates.  It is important to note that, because the
		//value of either 1/z or 1/w is referenced for the Z value, the closer
		//that the polygon is, the larger that the Z value will be.
		
		//This setting is ignored for Translucent polygons in Auto-sort 
		//mode; the comparison must be made on a "Greater or Equal" basis.  
		//This setting is also ignored for Punch Through polygons in HOLLY2; 
		//the comparison must be made on a "Less or Equal" basis.

#if 1
		XE_CMP_NEVER,				//0	Never
		XE_CMP_GREATER,		//1	Less
		XE_CMP_EQUAL,				//2	Equal
		XE_CMP_GREATEREQUAL,			//3	Less Or Equal
		XE_CMP_LESS,	//4	Greater
		XE_CMP_NOTEQUAL,			//5	Not Equal
		XE_CMP_LESSEQUAL,		//6	Greater Or Equal
		XE_CMP_ALWAYS,				//7	Always
#else
		XE_CMP_ALWAYS,				//7	Always
		XE_CMP_ALWAYS,				//7	Always
		XE_CMP_ALWAYS,				//7	Always
		XE_CMP_ALWAYS,				//7	Always
		XE_CMP_ALWAYS,				//7	Always
		XE_CMP_ALWAYS,				//7	Always
		XE_CMP_ALWAYS,				//7	Always
		XE_CMP_ALWAYS,				//7	Always
#endif
	};

	/*
	0	Zero	(0, 0, 0, 0)
	1	One	(1, 1, 1, 1)
	2	ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¹Ã…â€œOtherÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ Color	(OR, OG, OB, OA) 
	3	Inverse ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¹Ã…â€œOtherÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ Color	(1-OR, 1-OG, 1-OB, 1-OA)
	4	SRC Alpha	(SA, SA, SA, SA)
	5	Inverse SRC Alpha	(1-SA, 1-SA, 1-SA, 1-SA)
	6	DST Alpha	(DA, DA, DA, DA)
	7	Inverse DST Alpha	(1-DA, 1-DA, 1-DA, 1-DA)
	*/

	const static u32 DstBlendGL[] =
	{
		XE_BLEND_ZERO,
		XE_BLEND_ONE,
		XE_BLEND_SRCCOLOR,
		XE_BLEND_INVSRCCOLOR,
		XE_BLEND_SRCALPHA,
		XE_BLEND_INVSRCALPHA,
		XE_BLEND_DESTALPHA,
		XE_BLEND_INVDESTALPHA
	};

	const static u32 SrcBlendGL[] =
	{
		XE_BLEND_ZERO,
		XE_BLEND_ONE,
		XE_BLEND_DESTCOLOR,
		XE_BLEND_INVDESTCOLOR,
		XE_BLEND_SRCALPHA,
		XE_BLEND_INVSRCALPHA,
		XE_BLEND_DESTALPHA,
		XE_BLEND_INVDESTALPHA
	};

	char texFormatName[8][30]=
	{
		"1555",
		"565",
		"4444",
		"YUV422",
		"Bump Map",
		"4 BPP Palette",
		"8 BPP Palette",
		"Reserved	, 1555"
	};

	float unkpack_bgp_to_float[256];

	f32 f16(u16 v)
	{
		u32 z=v<<16;
		return *(f32*)&z;
	}
	const u32 MipPoint[8] =
	{
		0x00006,//8
		0x00016,//16
		0x00056,//32
		0x00156,//64
		0x00556,//128
		0x01556,//256
		0x05556,//512
		0x15556//1024
	};

#define twidle_tex(format)\
						if (tcw.NO_PAL.VQ_Comp)\
					{\
						u32 offset=0;\
                        static u8 swapped[1024*1024+0x15556];\
                        vq_codebook=(u8*)&params.vram[sa];\
						if (tcw.NO_PAL.MipMapped)\
                        {\
                            offset=MipPoint[tsp.TexV];\
                        }\
                        memcpy(swapped,&params.vram[sa],w*h+offset);\
                        bswap_block(swapped,((w*h+offset)&~3)+4);\
						format##to8888_VQ(&pbt,&swapped[offset],w,h);\
					}\
					else\
					{\
						if (tcw.NO_PAL.MipMapped)\
							sa+=MipPoint[tsp.TexU]<<3;\
						format##to8888_TW(&pbt,(u8*)&params.vram[sa],w,h);\
					}
#define norm_text(format) \
	u32 sr;\
	if (tcw.NO_PAL.StrideSel)\
					{sr=(TEXT_CONTROL&31)*32;}\
					else\
					{sr=w;}\
					format(&pbt,(u8*)&params.vram[sa],sr,h);

	typedef void fastcall texture_handler_FP(PixelBuffer* pb,u8* p_in,u32 Width,u32 Height);

    
// thank you ced2911 for this :)
static inline void handle_small_surface(struct XenosSurface * surf, void * buffer){
	int width;
	int height;
	int wpitch;
	int hpitch;
	uint32_t * surf_data;
	uint32_t * data;
	uint32_t * src;	
	
	// don't handle big texture
	if( surf->width>128 && surf->height>32) {
		return;
	}	
	
	width = surf->width;
	height = surf->height;
	wpitch = surf->wpitch / 4;
	hpitch = surf->hpitch;	
	
	if(buffer)
        surf_data = (uint32_t *)buffer;
    else
        surf_data = (uint32_t *)Xe_Surface_LockRect(xe, surf, 0, 0, 0, 0, XE_LOCK_WRITE);
	
	src = data = surf_data;
		
	for(int yp=0; yp<hpitch;yp+=height) {
		int max_h = height;
		if (yp + height> hpitch)
				max_h = hpitch % height;
		for(int y = 0; y<max_h; y++){
			//x order
			for(int xp = 0;xp<wpitch;xp+=width) {
				int max_w = width;
				if (xp + width> wpitch)
					max_w = wpitch % width;

				for(int x = 0; x<max_w; x++) {
					data[x+xp + ((y+yp)*wpitch)]=src[x+ (y*wpitch)];
				}
			}
		}
	}
	
    if(!buffer)
        Xe_Surface_Unlock(xe, surf);
}    
    
	/*
	texture_handler_FP* texture_handlers[8] = 
	{
		,//0	1555 value: 1 bit; RGB values: 5 bits each
		,//1	565	 R value: 5 bits; G value: 6 bits; B value: 5 bits
		,//3	YUV422 32 bits per 2 pixels; YUYV values: 8 bits each
		,//2	4444 value: 4 bits; RGB values: 4 bits each
		,//4	Bump Map	16 bits/pixel; S value: 8 bits; R value: 8 bits
		,//5	4 BPP Palette	Palette texture with 4 bits/pixel
		,//6	8 BPP Palette	Palette texture with 8 bits/pixel
		,//7 -> undefined , handled as 0
	};

	u32 texture_format[8]
	{
		D3DFMT_A1R5G5B5,//0	1555 value: 1 bit; RGB values: 5 bits each
		D3DFMT_R5G6B5,//1	565	 R value: 5 bits; G value: 6 bits; B value: 5 bits
		D3DFMT_UYVY,//3	YUV422 32 bits per 2 pixels; YUYV values: 8 bits each
		D3DFMT_A4R4G4B4,//2	4444 value: 4 bits; RGB values: 4 bits each
		D3DFMT_UNKNOWN,//4	Bump Map	16 bits/pixel; S value: 8 bits; R value: 8 bits
		D3DFMT_A8R8G8B8,//5	4 BPP Palette	Palette texture with 4 bits/pixel
		D3DFMT_A8R8G8B8,//6	8 BPP Palette	Palette texture with 8 bits/pixel
		D3DFMT_A1R5G5B5,//7 -> undefined , handled as 0
	};
	*/
	struct TextureCacheData;
	std::vector<TextureCacheData*> lock_list;
	//Texture Cache :)
	struct TextureCacheData
	{
		TCW tcw;TSP tsp;
		struct XenosSurface* Texture;
		u32 Lookups;
		u32 Updates;
		u32 LastUsed;
		u32 w,h;
		u32 size;
		bool dirty;
		u32 pal_rev;
		vram_block* lock_block;

		//Releases any resources , EXEPT the texture :)
		void Destroy()
		{
			if (lock_block)
				params.vram_unlock(lock_block);
			lock_block=0;
		}
		//Called when texture entry is reused , resets any texture type info (dynamic/static)
		void Reset()
		{
			Lookups=0;
			Updates=0;
		}
		void PrintTextureName()
		{
			printf(texFormatName[tcw.NO_PAL.PixelFmt]);
	
			if (tcw.NO_PAL.VQ_Comp)
				printf(" VQ");

			if (tcw.NO_PAL.ScanOrder==0)
				printf(" TW");

			if (tcw.NO_PAL.MipMapped)
				printf(" MM");

			if (tcw.NO_PAL.StrideSel)
				printf(" Stride");

			if (tcw.NO_PAL.StrideSel)
				printf(" %d[%d]x%d @ 0x%X",(TEXT_CONTROL&31)*32,8<<tsp.TexU,8<<tsp.TexV,tcw.NO_PAL.TexAddr<<3);
			else
				printf(" %dx%d @ 0x%X",8<<tsp.TexU,8<<tsp.TexV,tcw.NO_PAL.TexAddr<<3);
			printf("\n");
		}
		void Update()
		{
//			verify(dirty);
//			verify(lock_block==0);

			LastUsed=FrameNumber;
			Updates++;
			dirty=false;

			u32 sa=(tcw.NO_PAL.TexAddr<<3) & VRAM_MASK;

			if (Texture==0)
			{
				if (tcw.NO_PAL.MipMapped && (!(drkpvr_settings.Emulation.PaletteMode>=2 && (tcw.NO_PAL.PixelFmt==5 || tcw.NO_PAL.PixelFmt==6))) )
				{
					Texture=Xe_CreateTexture(xe,w,h,0,XE_FMT_8888|XE_FMT_ARGB,0);
				}
				else
				{
					Texture=Xe_CreateTexture(xe,w,h,0,XE_FMT_8888|XE_FMT_ARGB,0);
				}
			}
			
			void * bits=Xe_Surface_LockRect(xe,Texture,0,0,0,0,XE_LOCK_WRITE);

			PixelBuffer pbt; 
			pbt.init(bits,Texture->wpitch);
			
			switch (tcw.NO_PAL.PixelFmt)
			{
			case 0:
			case 7:
				//0	1555 value: 1 bit; RGB values: 5 bits each
				//7	Reserved	Regarded as 1555
				if (tcw.NO_PAL.ScanOrder)
				{
					//verify(tcw.NO_PAL.VQ_Comp==0);
					norm_text(argb1555to8888);
					//argb1555to8888(&pbt,(u16*)&params.vram[sa],w,h);
				}
				else
				{
					//verify(tsp.TexU==tsp.TexV);
					twidle_tex(argb1555);
				}
				break;

				//redo_argb:
				//1	565	 R value: 5 bits; G value: 6 bits; B value: 5 bits
			case 1:
				if (tcw.NO_PAL.ScanOrder)
				{
					//verify(tcw.NO_PAL.VQ_Comp==0);
					norm_text(argb565to8888);
					//(&pbt,(u16*)&params.vram[sa],w,h);
				}
				else
				{
					//verify(tsp.TexU==tsp.TexV);
					twidle_tex(argb565);
				}
				break;

				
				//2	4444 value: 4 bits; RGB values: 4 bits each
			case 2:
				if (tcw.NO_PAL.ScanOrder)
				{
					//verify(tcw.NO_PAL.VQ_Comp==0);
					//argb4444to8888(&pbt,(u16*)&params.vram[sa],w,h);
					norm_text(argb4444to8888);
				}
				else
				{
					twidle_tex(argb4444);
				}

				break;
				//3	YUV422 32 bits per 2 pixels; YUYV values: 8 bits each
			case 3:
				if (tcw.NO_PAL.ScanOrder)
				{
					norm_text(YUV422to8888);
					//norm_text(ANYtoRAW);
				}
				else
				{
					//it cant be VQ , can it ?
					//docs say that yuv can't be VQ ...
					//HW seems to support it ;p
					twidle_tex(YUV422);
				}
				break;
				//4	Bump Map	16 bits/pixel; S value: 8 bits; R value: 8 bits
			case 5:
				//5	4 BPP Palette	Palette texture with 4 bits/pixel
				verify(tcw.PAL.VQ_Comp==0);
				if (tcw.NO_PAL.MipMapped)
							sa+=MipPoint[tsp.TexU]<<1;
				palette_index = tcw.PAL.PalSelect<<4;
				pal_rev=pal_rev_16[tcw.PAL.PalSelect];
				if (drkpvr_settings.Emulation.PaletteMode<2)
				{
					PAL4to8888_TW(&pbt,(u8*)&params.vram[sa],w,h);
				}
				else
				{
					PAL4toX444_TW(&pbt,(u8*)&params.vram[sa],w,h);
				}

				break;
			case 6:
				//6	8 BPP Palette	Palette texture with 8 bits/pixel
				verify(tcw.PAL.VQ_Comp==0);
				if (tcw.NO_PAL.MipMapped)
							sa+=MipPoint[tsp.TexU]<<2;
				palette_index = (tcw.PAL.PalSelect<<4)&(~0xFF);
				pal_rev=pal_rev_256[tcw.PAL.PalSelect>>4];
				if (drkpvr_settings.Emulation.PaletteMode<2)
				{
					PAL8to8888_TW(&pbt,(u8*)&params.vram[sa],w,h);
				}
				else
				{
					PAL8toX444_TW(&pbt,(u8*)&params.vram[sa],w,h);
				}
				break;
			default:
				printf("Unhandled texture\n");
				//memset(temp_tex_buffer,0xFFEFCFAF,w*h*4);
			}

            
            handle_small_surface(Texture,bits);
                    
    		//done , unlock texture !
			Xe_Surface_Unlock(xe,Texture);

			//PrintTextureName();
			if (!lock_block)
				lock_list.push_back(this);
			/*
			char file[512];

			sprintf(file,"g:\\textures\\0x%08x_%08x_%s_%d_VQ%d_TW%d_MM%d_.png",tcw.full,tsp.full,texFormatName[tcw.NO_PAL.PixelFmt],Lookups
			,tcw.NO_PAL.VQ_Comp,tcw.NO_PAL.ScanOrder,tcw.NO_PAL.MipMapped);
			D3DXSaveTextureToFileA( file,D3DXIFF_PNG,Texture,0);
			*/
		}
		void LockVram()
		{
			u32 sa=(tcw.NO_PAL.TexAddr<<3) & VRAM_MASK;
			u32 ea=sa+w*h*2;
			if (ea>VRAM_MASK)
			{
				ea=VRAM_MASK;
			}
			lock_block = params.vram_lock_64(sa,ea,this);
		}
	};

	TexCacheList<TextureCacheData> TexCache;
	TexCacheList<TextureCacheData> TexCache_Discard;

	TextureCacheData* __fastcall GenText(TSP tsp,TCW tcw,TextureCacheData* tf)
	{
		//generate texture	
		tf->w=8<<tsp.TexU;
		tf->h=8<<tsp.TexV;
		tf->tsp=tsp;
		tf->tcw=tcw;
		tf->dirty=true;
		tf->lock_block=0;
		tf->Texture=0;
		tf->Reset();
		tf->Update();
		return tf;
	}

	TextureCacheData* __fastcall GenText(TSP tsp,TCW tcw)
	{
		//add new entry to tex cache
		TextureCacheData* tf = &TexCache.Add(0)->data;
		//Generate texture 
		return GenText(tsp,tcw,tf);
	}

	u32 RenderToTextureAddr;

	struct XenosSurface * __fastcall GetTexture(TSP tsp,TCW tcw)
	{	
		u32 addr=(tcw.NO_PAL.TexAddr<<3) & VRAM_MASK;
//		printf("GetTexture %08x %08x %08x\n",tsp.full,tcw.full,addr);
		if (addr==rtt_address)
		{
			rtt_FrameNumber=FrameNumber;
			//printf("Reading to %d\n",rtt_index);
			return rtt_texture[rtt_index];
		}

		//EnterCriticalSection(&tex_cache_cs);
		TextureCacheData* tf = TexCache.Find(tcw.full,tsp.full);
		if (tf)
		{
			tf->LastUsed=FrameNumber;
			if (tf->dirty)
			{
				tf->Update();
			}
			if (drkpvr_settings.Emulation.PaletteMode==1)
			{
				if (tcw.PAL.PixelFmt==5)
				{				
					if (tf->pal_rev!=pal_rev_16[tcw.PAL.PalSelect])
						tf->Update();
				}
				else if (tcw.PAL.PixelFmt==6)
				{
					if (tf->pal_rev!=pal_rev_256[tcw.PAL.PalSelect>>4])
						tf->Update();
				}
			}
			tf->Lookups++;
			//LeaveCriticalSection(&tex_cache_cs);
			return tf->Texture;
		}
		else
		{
			tf = GenText(tsp,tcw);
			//LeaveCriticalSection(&tex_cache_cs);
			return tf->Texture;
		}
		return 0;
	}
	
	void VramLockedWrite(vram_block* bl,u32 addr)
	{
		TextureCacheData* tcd = (TextureCacheData*)bl->userdata;

    	//if (addr>=bl->start && addr<=bl->end)
        {
//            printf("inval %d\n",tcd->Texture->height*tcd->Texture->wpitch);
                        
            tcd->dirty=true;
            tcd->lock_block=0;
    		params.vram_unlock(bl);
        }
	}
	extern cThread rth;

	u32 vri(u32 addr);
	//use that someday
	/*
	Vertex FullScreenQuad[4] = 
	{
		{0,0,0.5 ,0,0,0,0 ,0,0,0,0 },
		{0,0,0.5 ,0,0,0,0 ,0,0,0,0 },
		{0,0,0.5 ,0,0,0,0 ,0,0,0,0 },
		{0,0,0.5 ,0,0,0,0 ,0,0,0,0 },
	};
	*/
	void DrawOSD();
	void VBlank()
	{
		FrameNumber++;
		
/*		u32 field=0;//default from field 1
//		u32 interlc=SPG_CONTROL.interlace;
		switch (VO_CONTROL.field_mode)
		{
			//From SPG
		case 0:
			field=SPG_STATUS.fieldnum;
			break;

			//From ~SPG
		case 1:
			field=1^SPG_STATUS.fieldnum;
			break;

			//From field 1
		case 2:
			field=0;
			break;

			//From field 2
		case 3:
			field=1;
			break;

			//From field 1 when HSYNC && VSYNC match
		case 4:
			field=0;	//uh. ...
			break;

			//From field 2 when HSYNC && VSYNC match
		case 5:
			field=1;	//uh. ...
			break;

			//From field 1 when HSYNC -> 1 in VSYNC
		case 6:
			field=0;	//uh. ...
			break;

			//From field 2 when HSYNC -> 1 in VSYNC
		case 7:
			field=1;	//uh. ...
			break;

			//Inverted when VSYNC -> 1
		case 8:
			//what ?
			break;

		default:
			break;
		}

		u32 src;
		//select input ..
		if (field)
			src=FB_R_SOF2;
		else
			src=FB_R_SOF1;*/

//		u32 addr1=vramlock_ConvOffset32toOffset64(src);
//		u32* ptest=(u32*)&params.vram[addr1];

		{
#if 0
			struct XenosSurface * tex=fb_texture;
			//assume rect is same as front buffer
			RECT rs={0,0,fb_surf_desc.Width,fb_surf_desc.Height};
			
			dev->ColorFill(backbuffer,0,D3DCOLOR_ARGB(255,VO_BORDER_COL.Red,VO_BORDER_COL.Green,VO_BORDER_COL.Blue));

			if ((FB_R_CTRL.fb_enable && !VO_CONTROL.blank_video) || DC_PLATFORM==DC_PLATFORM_ATOMISWAVE)
			{
				if ( *ptest!=0xDEADC0DE)
				{
					//use proper FB rect size
					rs.right=640;
					rs.bottom=480;

					D3DLOCKED_RECT lr;
					u32 bpp;
					switch(FB_R_CTRL.fb_depth)
					{
					case fbde_0555:
						bpp=2;
						surf=fb_surface1555;
						tex=fb_texture1555;
						break;
					case fbde_565:
						bpp=2;
						surf=fb_surface565;
						tex=fb_texture565;
						break;
					case fbde_C888:
						bpp=4;
						surf=fb_surface8888;
						tex=fb_texture8888;
						break;
					case fbde_888:
						bpp=3;
						surf=fb_surface8888;
						tex=fb_texture8888;
						break;
					}

					u32 PixelsPerLine=640;

					verifyc(surf->LockRect(&lr,0,0));


					u32 out_field=SPG_STATUS.fieldnum;

					u32 fb_skip=FB_R_SIZE.fb_modulus;
					u32 line_double=FB_R_CTRL.fb_line_double;
					u32 pixel_double=VO_CONTROL.pixel_double;

					if (pixel_double)
						PixelsPerLine/=2;
					//neat trick to detect single framebuffer interlacing
					if (interlc==1)
					{
						int diff=FB_R_SOF2-FB_R_SOF1;
						if (diff<0)
							diff=-diff;
						if (line_double)
							diff/=2;
						if ((diff/4)==(fb_skip-1))
						{
							src=FB_R_SOF1;
							fb_skip=1;
							interlc=0;
						}
					}
					else
					{
						//half lines needed on non-interlaced mode on non VGA streams
						if (!FB_R_CTRL.vclk_div)
						{
							rs.bottom/=2;
						}
					}

					u32* read=(u32*)&params.vram[vramlock_ConvOffset32toOffset64(src)];
					u32* read_line=read;

					u32* write=(u32*)lr.pBits;
					u32* line=(u32*)lr.pBits;

					if (pixel_double)
						rs.right/=2;
					if (line_double)
						rs.bottom/=2;

					u32 DWordsPerLine=PixelsPerLine*bpp/4;

					for (u32 y=0;y<(u32)rs.bottom;y+=1)
					{
						if (!interlc || out_field==(y&1))
						{
							if (bpp==3)
							{
								u8* br=(u8*)read;
								u8* bw=(u8*)write;
								for (u32 x=0;x<PixelsPerLine;x+=1)
								{
									*bw++=*br++;
									if (((u32)br&3)==0)
										br+=4;
									*bw++=*br++;
									if (((u32)br&3)==0)
										br+=4;
									*bw++=*br++;
									if (((u32)br&3)==0)
										br+=4;

									*bw++=0;	//skip A
								}
							}
							else
							{
								for (u32 x=0;x<DWordsPerLine;x+=1)
								{
									*write++=*read;
									read+=2;//skip the 'other' bank
								}
							}
							read_line+=(DWordsPerLine-1)*2;//*2 to skip the 'other' bank.-1 so that it points to the last pixel of the last line
							read_line+=fb_skip*2;//*2 to skip the 'other' bank
							read=read_line;
						}

						line+=lr.Pitch/4;
						write=line;
					}

					verifyc(surf->UnlockRect());
				}
			}
			else
			{
				tex=0;
				surf=0;
			}

			//if 0, then simply colorfill (fb is off)
			if (surf!=0)
			{
				//perform aspect ratio matching here ..
				//this is wayyyy wayy way way way more complicated than it needs, its what psy calls razimaths(tm)
				//it actually works
				void CalcRect(float* dst,float* src);
				float rdf[2]={(float)rs.right,(float)rs.bottom};
				float rsf[2]={(float)ppar.BackBufferWidth,(float)ppar.BackBufferHeight};
				RECT rd;
				CalcRect(rdf,rsf);
				rd.right=(LONG)(0.5f+rs.right/rdf[0]*rsf[0]);
				rd.bottom=(LONG)(0.5f+rs.bottom/rdf[1]*rsf[1]);
				rd.left=(LONG)(abs((LONG)ppar.BackBufferWidth-rd.right)/2);
				rd.top=(LONG)(abs((LONG)ppar.BackBufferHeight-rd.bottom)/2);
				
				rd.right+=rd.left;
				rd.bottom+=rd.top;
				if (rd.right>(LONG)ppar.BackBufferWidth)
					rd.right=(LONG)ppar.BackBufferWidth;

				if (rd.bottom>(LONG)ppar.BackBufferHeight)
					rd.bottom=(LONG)ppar.BackBufferHeight;

				dev->StretchRect(surf,&rs,backbuffer,&rd, D3DTEXF_LINEAR);	//add an option for D3DTEXF_POINT for pretty pixels?
			}

			dev->SetRenderTarget(0,backbuffer);
			dev->SetTexture(0,tex);
			dev->SetTexture(1,tex);
			dev->SetVertexShader(Composition.vs);
			dev->SetPixelShader(Composition.ps_DrawFB);
			
			float fscale[8]=
			{
				1/320.f,-1.0f/240.f,320,240,
				1/320.f,-1.0f/240.f,320,240
			};
			dev->SetVertexShaderConstantF(0,fscale,2);
			dev->SetPixelShaderConstantF(0,fscale,2);

			f32 fsq[] = 
			{
				0		,0		,0.5,1,		1,1,1,1,	0,0,
				0		,480	,0.5,1,		1,1,1,1,	0,1,
				640		,0		,0.5,1,		1,1,1,1,	1,0,
				640		,480	,0.5,1,		1,1,1,1,	1,1,
			};

			dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
			verifyc(dev->SetVertexDeclaration(vdecl_osd));
			
			dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
			dev->SetRenderState(D3DRS_ZENABLE,FALSE);

			//(dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,fsq,10*4));

			DrawOSD();
			(dev->Present(0,0,0,0));
#endif
		}
	}

	//Vertex storage types
	//64B
	struct Vertex
	{
		//64
		float x,y,z;

#ifdef _float_colors_
		float col[4];
		float spc[4];
#else
		u32 col;
		u32 spc;
#endif
		float u,v;
		/*float p1,p2,p3;	*/ //pad to 64 bytes (for debugging purposes)
		#ifdef _HW_INT_
			float base_int,offset_int;
		#endif
	};
	Vertex* BGPoly;

	struct PolyParam
	{
		u32 first;		//entry index , holds vertex/pos data
		u32 count;

		//lets see what more :)
		
		TSP tsp;
		TCW tcw;
		PCW pcw;
		ISP_TSP isp;
		float zvZ;
		u32 tileclip;
		//float zMin,zMax;
	};
	struct ModParam
	{
		u32 first;		//entry index , holds vertex/pos data
		u32 count;
	};
	struct ModTriangle
	{
		f32 x0,y0,z0,x1,y1,z1,x2,y2,z2;
	};


	static Vertex* vert_reappend;

	union ISP_Modvol
	{
		struct
		{
			u32 id:26;
			u32 VolumeLast:1;
			u32	CullMode	: 2;
			u32	DepthMode	: 3;
		};
		u32 full;
	};
	//vertex lists
	struct TA_context
	{
		u32 Address;
		u32 LastUsed;
		f32 invW_min;
		f32 invW_max;
		List2<Vertex> verts;
		List<ModTriangle>	modtrig;
		List<ISP_Modvol>	global_param_mvo;

		List<PolyParam> global_param_op;
		List<PolyParam> global_param_pt;
		List<PolyParam> global_param_tr;

		void Init()
		{
			verts.Init();
			global_param_op.Init();
			global_param_pt.Init();
			global_param_mvo.Init();
			global_param_tr.Init();

			modtrig.Init();
		}
		void Clear()
		{
			verts.Clear();
			global_param_op.Clear();
			global_param_pt.Clear();
			global_param_tr.Clear();
			modtrig.Clear();
			global_param_mvo.Clear();
			invW_min= 1000000.0f;
			invW_max=-1000000.0f;
		}
		void Free()
		{
			verts.Free();
			global_param_op.Free();
			global_param_pt.Free();
			global_param_tr.Free();
			modtrig.Free();
			global_param_mvo.Free();
		}
	};
	
	
	bool UsingAutoSort()
	{
		if (((FPU_PARAM_CFG>>21)&1) == 0)
			return ((ISP_FEED_CFG&1)==0);
		else
			return ( (vri(REGION_BASE)>>29) & 1) == 0;
	}

	TA_context tarc;
	TA_context pvrrc;
bool operator<(const PolyParam &left, const PolyParam &right)
{
/* put any condition you want to sort on here */
	return left.zvZ<right.zvZ;
	//return left.zMin<right.zMax;
}
	void SortPParams()
	{
		if (pvrrc.verts.allocate_list_sz->size()==0)
			return;

//		u32 base=0;
		u32 csegc=0;
		u32 cseg=-1;
		Vertex* bptr=0;
		for (u32 i=0;i<pvrrc.global_param_tr.used;i++)
		{
			u32 s=pvrrc.global_param_tr.data[i].first;
			u32 c=pvrrc.global_param_tr.data[i].count;

			
			//float zmax=-66666666666,zmin=66666666666;
			float zv=0;
			for (u32 j=s;j<(s+c);j++)
			{
				while (j>=csegc)
				{
					cseg++;
					bptr=(Vertex*)((*pvrrc.verts.allocate_list_ptr)[cseg]);
					bptr-=csegc;
					csegc+=(*pvrrc.verts.allocate_list_sz)[cseg]/sizeof(Vertex);
				}
				zv+=bptr[j].z;
				/*if (zmax<bptr[j].z)
					zmax=bptr[j].z;
				if (zmin>bptr[j].z)
					zmin=bptr[j].z;*/
					
			}
			pvrrc.global_param_tr.data[i].zvZ=zv/c;
			/*pvrrc.global_param_tr.data[i].zMax=zmax;
			pvrrc.global_param_tr.data[i].zMin=zmin;*/
		}

		std::stable_sort(pvrrc.global_param_tr.data,pvrrc.global_param_tr.data+pvrrc.global_param_tr.used);
	}

	std::vector<TA_context> rcnt;
	u32 fastcall FindRC(u32 addr)
	{
		for (u32 i=0;i<rcnt.size();i++)
		{
			if (rcnt[i].Address==addr)
			{
				return i;
			}
		}
		return 0xFFFFFFFF;
	}
	void fastcall SetCurrentTARC(u32 addr)
	{
		addr&=0xF00000;
		//return;
//		printf("SetCurrentTARC:0x%X\n",addr);
		if (addr==tarc.Address)
			return;//nothing to do realy

		//save old context
		u32 found=FindRC(tarc.Address);
		if (found!=0xFFFFFFFF)
        {
            rcnt[found]=tarc;
        }

		//switch to new one
		found=FindRC(addr);
		if (found!=0xFFFFFFFF)
		{
            tarc=rcnt[found];
		}
		else
		{
			//add one :p
			tarc.Address=addr;
			tarc.Init();
			tarc.Clear();
			rcnt.push_back(tarc);
		}
	}
	void fastcall SetCurrentPVRRC(u32 addr)
	{
		addr&=0xF00000;
		//return;
//		printf("SetCurrentPVRRC:0x%X\n",addr);
		if (addr==tarc.Address)
		{
            pvrrc=tarc;
			return;
		}

		u32 found=FindRC(addr);
		if (found!=0xFFFFFFFF)
		{
            pvrrc=rcnt[found];
			return;
		}

		printf("WARNING : Unable to find a PVR rendering context\n");
        pvrrc=tarc;
	}


	PolyParam* CurrentPP=0;
	List<PolyParam>* CurrentPPlist;
	
	TSP cache_tsp;
	TCW cache_tcw;
	//PCW cache_pcw;
	ISP_TSP cache_isp;
	u32 cache_clipmode=0xFFFFFFFF;
	bool cache_clip_alpha_on_zero=true;
	u32 cache_texture_enabled=0;
	u32 cache_stencil_modvol_on=false;
	void GPstate_cache_reset(PolyParam* gp)
	{
		cache_tsp.full = ~gp->tsp.full;
		cache_tcw.full = ~gp->tcw.full;
		//cache_pcw.full = ~gp->pcw.full;
		cache_isp.full = ~gp->isp.full;
		cache_clipmode=0xFFFFFFFF;
		cache_clip_alpha_on_zero=true;
		cache_texture_enabled=~gp->pcw.Texture;
		cache_stencil_modvol_on=0;
		last_ps_mode=-1;
	}

	//fox pixel shaders

	//Texture -> 1 if texture is enabled , 0 if its not
	//Offset -> 1 if offset is enabled , 0 if its not (only valid when texture is enabled)
	//ShadInstr -> 0 to 3 , see pvr docs , valid only when texture is enabled
	//IgnoreTexA -> 1 if on  0 if off , valid only w/ textures on
	//UseAlpha -> 1 if on  0 if off , works when no textures are used too ?

	#define idx_pp_Texture 0
	#define idx_pp_Offset 1
	#define idx_pp_ShadInstr 2
	#define idx_pp_IgnoreTexA 3
	#define idx_pp_UseAlpha 4
	#define idx_pp_TextureLookup 5
	#define idx_ZBufferMode 6
	#define idx_pp_FogCtrl 7

	float cur_pal_index[4]={0,0,0,1};
	INLINE
	void SetGPState_ps(PolyParam* gp)
	{
		if (gp->pcw.Texture)
		{
			Xe_SetPixelShaderConstantB(xe,8,0);
			Xe_SetPixelShaderConstantB(xe,9,0);

			if (drkpvr_settings.Emulation.PaletteMode>1)
			{
				u32 pal_mode=drkpvr_settings.Emulation.PaletteMode-1;
				if (pal_mode==2 && gp->tsp.FilterMode==0)
					pal_mode=1;//no filter .. ugh
					
				u32 pf=gp->tcw.PAL.PixelFmt;
				if (pf==5)
				{
					cur_pal_index[1]=gp->tcw.PAL.PalSelect/64.0f;
					Xe_SetPixelShaderConstantB(xe,8,pal_mode&1);
					Xe_SetPixelShaderConstantB(xe,9,pal_mode&2);
					Xe_SetPixelShaderConstantF(xe,0,cur_pal_index,1);
				}
				else if (pf==6)
				{
					cur_pal_index[1]=(gp->tcw.PAL.PalSelect&~0xF)/64.0f;
					Xe_SetPixelShaderConstantB(xe,8,pal_mode&1);
					Xe_SetPixelShaderConstantB(xe,9,pal_mode&2);
					Xe_SetPixelShaderConstantF(xe,0,cur_pal_index,1);
				}
			}
			
			Xe_SetPixelShaderConstantB(xe,1,gp->pcw.Offset);

			Xe_SetPixelShaderConstantB(xe,2,gp->tsp.ShadInstr&1);
			Xe_SetPixelShaderConstantB(xe,3,gp->tsp.ShadInstr&2);

			Xe_SetPixelShaderConstantB(xe,4,gp->tsp.IgnoreTexA);
		}

		Xe_SetPixelShaderConstantB(xe,0,gp->pcw.Texture);

		Xe_SetPixelShaderConstantB(xe,5,gp->tsp.UseAlpha);

		Xe_SetPixelShaderConstantB(xe,6,gp->tsp.FogCtrl&1);
		Xe_SetPixelShaderConstantB(xe,7,gp->tsp.FogCtrl&2);
	}

	//realy only uses bit0, destroys all of em atm :]

	void SetTileClip(u32 val)
	{
		if (cache_clipmode==val)
			return;
		cache_clipmode=val;

		u32 clipmode=val>>28;
		
		if (clipmode<2 ||clipmode&1 )
		{
			Xe_SetClipPlaneEnables(xe,0);
		}
		else
		{
			clipmode&=1; 
			/*if (clipmode&1)
				dev->SetRenderState(D3DRS_CLIPPLANEENABLE,3);
			else*/
			Xe_SetClipPlaneEnables(xe,0xf);

			float x_min=(float)(val&63);
			float x_max=(float)((val>>6)&63);
			float y_min=(float)((val>>12)&31);
			float y_max=(float)((val>>17)&31);
			x_min=x_min*32;
			x_max=x_max*32 +31.999f;	//+31.999 to do [min,max)
			y_min=y_min*32;
			y_max=y_max*32 +31.999f;
			
			x_min+=current_scalef[0];
			x_max+=current_scalef[0];
			y_min+=current_scalef[1];
			y_max+=current_scalef[1];

			x_min/=current_scalef[2];
			x_max/=current_scalef[2];
			y_min/=current_scalef[3];
			y_max/=current_scalef[3];

			//Ax + By + Cz + Dw
			float v[4];
			float clips=1.0f-clipmode*2;
			v[0]=clips;
			v[1]=0;
			v[2]=0;
			v[3]=-(x_min-1)*clips ;
			
			Xe_SetClipPlane(xe,0,v);

			v[0]=-clips;
			v[1]=0;
			v[2]=0;
			v[3]=(x_max-1)*clips ;

			Xe_SetClipPlane(xe,1,v);

			v[0]=0;
			v[1]=-clips;
			v[2]=0;
			v[3]=(1+y_min)*clips;

			Xe_SetClipPlane(xe,2,v);

			v[0]=0;
			v[1]=clips;
			v[2]=0;
			v[3]=-(1+y_max)*clips ;

			Xe_SetClipPlane(xe,3,v);
		}
	}
	const float TextureSizes[8][2]=
	{
		{8.f,1/8.f},
		{16.f,1/16.f},
		{32.f,1/32.f},
		{64.f,1/64.f},
		{128.f,1/128.f},
		{256.f,1/256.f},
		{512.f,1/512.f},
		{1024.f,1/1024.f},
	};
	//
	template <u32 Type,bool FFunction,bool df,bool SortingEnabled>
	INLINE
	void SetGPState(PolyParam* gp,u32 cflip=0)
	{	/*
		if (gp->tsp.DstSelect ||
			gp->tsp.SrcSelect)
			printf("DstSelect  DstSelect\n"); */

		SetTileClip(gp->tileclip);
		//has to preserve cache_tsp/cache_isp
		//can freely use cache_tcw
		SetGPState_ps(gp);

		const u32 stencil=(gp->pcw.Shadow!=0)?0x80:0;
		if (cache_stencil_modvol_on!=stencil && drkpvr_settings.Emulation.ModVolMode==MVM_NormalAndClip)
		{
			cache_stencil_modvol_on=stencil;
			Xe_SetStencilRef(xe,3,stencil); //Clear/Set bit 7 (Clear for non 2 volume stuff)
		}
		

		if ((gp->tcw.full != cache_tcw.full) || (gp->tsp.full!=cache_tsp.full) || (cache_texture_enabled!= gp->pcw.Texture))
		{
			cache_tcw=gp->tcw;
			cache_texture_enabled = gp->pcw.Texture;
			
            if (gp->tsp.full!=cache_tsp.full)
            {
                cache_tsp=gp->tsp;

                if (Type==ListType_Translucent)
                {
                    Xe_SetSrcBlend(xe,SrcBlendGL[gp->tsp.SrcInstr]);
                    Xe_SetSrcBlendAlpha(xe,SrcBlendGL[gp->tsp.SrcInstr]);
                    Xe_SetDestBlend(xe,DstBlendGL[gp->tsp.DstInstr]);
                    Xe_SetDestBlendAlpha(xe,DstBlendGL[gp->tsp.DstInstr]);
                    bool clip_alpha_on_zero=gp->tsp.SrcInstr==4 && (gp->tsp.DstInstr==1 || gp->tsp.DstInstr==5);
                    if (clip_alpha_on_zero!=cache_clip_alpha_on_zero)
                    {
                        cache_clip_alpha_on_zero=clip_alpha_on_zero;
                        Xe_SetAlphaTestEnable(xe,clip_alpha_on_zero);
                    }
                }
            }

			if (gp->pcw.Texture)
			{
                struct XenosSurface * tex=GetTexture(gp->tsp,gp->tcw);

                if ( gp->tsp.FilterMode == 0 || (drkpvr_settings.Emulation.PaletteMode>1 && ( gp->tcw.PAL.PixelFmt==5|| gp->tcw.PAL.PixelFmt==6) ))
                {
                    tex->use_filtering=0;
                }
                else
                {
                    tex->use_filtering=1;
                }

                if (gp->tsp.ClampV)
                    tex->v_addressing=XE_TEXADDR_CLAMP;
                else 
                {
                    if (gp->tsp.FlipV)
                        tex->v_addressing=XE_TEXADDR_MIRROR;
                    else
                        tex->v_addressing=XE_TEXADDR_WRAP;
                }

                if (gp->tsp.ClampU)
                    tex->u_addressing=XE_TEXADDR_CLAMP;
                else 
                {
                    if (gp->tsp.FlipU)
                        tex->u_addressing=XE_TEXADDR_MIRROR;
                    else
                        tex->u_addressing=XE_TEXADDR_WRAP;
                }

                Xe_SetTexture(xe,0,tex);
				float tsz[4];
				tsz[0]=TextureSizes[gp->tsp.TexU][0];
				tsz[2]=TextureSizes[gp->tsp.TexU][1];
				tsz[1]=TextureSizes[gp->tsp.TexV][0];
				tsz[3]=TextureSizes[gp->tsp.TexV][1];

				Xe_SetPixelShaderConstantF(xe,1,tsz,1);
				Xe_SetVertexShaderConstantF(xe,3,tsz,1);
			}
		}

		if (df)
		{
			Xe_SetCullMode(xe,CullMode[gp->isp.CullMode+cflip]);
		}
		if (gp->isp.full!= cache_isp.full)
		{
			cache_isp.full=gp->isp.full;
			//set cull mode !
			if (!df)
				Xe_SetCullMode(xe,CullMode[gp->isp.CullMode]);
			//set Z mode !
			if (Type==ListType_Opaque)
			{
				Xe_SetZFunc(xe,Zfunction[gp->isp.DepthMode]);
			}
			else if (Type==ListType_Translucent)
			{
				if (SortingEnabled)
					Xe_SetZFunc(xe,Zfunction[6]); // : GEQ
				else
					Xe_SetZFunc(xe,Zfunction[gp->isp.DepthMode]);
			}
			else
			{
				//gp->isp.DepthMode=6;
				Xe_SetZFunc(xe,Zfunction[6]); //PT : LEQ //GEQ ?!?! wtf ? seems like the docs ARE wrong on this one =P
			}

			Xe_SetZWrite(xe,gp->isp.ZWriteDis==0);
		}
	}
	template <u32 Type,bool FFunction,bool SortingEnabled>
	INLINE
	void RendStrips(PolyParam* gp)
	{
			SetGPState<Type,FFunction,false,SortingEnabled>(gp);
			if (gp->count>2)//0 vert polys ? why does games even bother sending em  ? =P
			{		
				Xe_DrawPrimitive(xe,XE_PRIMTYPE_TRIANGLESTRIP,gp->first,gp->count-2); // -2 for vtx count to prim count
				hadTriangles=true;
			}
	}

	template <u32 Type,bool FFunction,bool SortingEnabled>
	void RendPolyParamList(List<PolyParam>& gpl)
	{
		if (gpl.used==0)
			return;
		//we want at least 1 PParam

		//reset the cache state
		GPstate_cache_reset(&gpl.data[0]);

		for (u32 i=0;i<gpl.used;i++)
		{		
			RendStrips<Type,FFunction,SortingEnabled>(&gpl.data[i]);
		}
	}
	
	struct SortTrig
	{
		f32 z;
		u32 id;
		PolyParam* pparam;
	};

	bool operator<(const SortTrig &left, const SortTrig &right)
	{
		/* put any condition you want to sort on here */
		return left.z<right.z;
		//return left.zMin<right.zMax;
	}

	vector<SortTrig> sorttemp;


	//sort and render , only for alpha blended stuff
	template <bool FFunction>
	void SortRendPolyParamList(List<PolyParam>& gpl)
	{
		if (gpl.used==0)
			return;
		//we want at least 1 PParam

		sorttemp.reserve(pvrrc.verts.used>>2);

		if (pvrrc.verts.allocate_list_sz->size()==0)
			return;

	//	u32 base=0;
		u32 csegc=0;
		u32 cseg=-1;
		Vertex* bptr=0;
	//	f32 t1=0;
		for (u32 i=0;i<pvrrc.global_param_tr.used;i++)
		{
			PolyParam* gp=&gpl.data[i];
			if (gp->count<3)
				continue;
			u32 s=gp->first;
			u32 c=gp->count-2;

		
			//float *p1=&t1,*p2=&t1;
			for (u32 j=s;j<(s+c);j++)
			{
				while (j>=csegc)
				{
					cseg++;
					bptr=(Vertex*)((*pvrrc.verts.allocate_list_ptr)[cseg]);
					bptr-=csegc;
					csegc+=(*pvrrc.verts.allocate_list_sz)[cseg]/sizeof(Vertex);
				}
				SortTrig t;
				t.z=bptr[j].z;//+bptr[j+1].z+bptr[j+2].z;
				//*p1+=t.z;
				//*p2+=t.z;

				t.id=j;
				t.pparam=gp;
				
				sorttemp.push_back(t);
				//p1=p2;
				//p2=&sorttemp[sorttemp.size()-1].z;
			}
		}
		if (sorttemp.size()==0)
			return;
		stable_sort(&sorttemp[0],&sorttemp[0]+sorttemp.size());

		//reset the cache state
		GPstate_cache_reset(sorttemp[0].pparam);

		for (u32 i=0;i<sorttemp.size();i++)
		{
			u32 fl=((sorttemp[i].id - sorttemp[i].pparam->first)&1)<<2;
			SetGPState<ListType_Translucent,FFunction,true,true>(sorttemp[i].pparam,fl);
			Xe_DrawPrimitive(xe,XE_PRIMTYPE_TRIANGLESTRIP,sorttemp[i].id,1);
			hadTriangles=true;
		}
		//printf("%d Render calls\n",sorttemp.size());
		sorttemp.clear();
	}

	void DrawOSD()
	{
	}
	//
	void UpdatePaletteTexure()
	{
		palette_update();
		if (pal_texture==0)
			return;

		//pal is organised as 16x64 texture
		u8* tex=(u8*)Xe_Surface_LockRect(xe,pal_texture,0,0,0,0,XE_LOCK_WRITE);
        u8* bits=tex;
		u8* src=(u8*)palette_lut;

		for (int i=0;i<64;i++)
		{
			memcpy(tex,src,16*4);
			tex+=pal_texture->wpitch;
			src+=16*4;
		}
        
        handle_small_surface(pal_texture,bits);
        
		Xe_Surface_Unlock(xe,pal_texture);
	}
	void UpdateFogTableTexure()
	{
		if (fog_texture==0)
			return;
		
		//fog is 128x1 texure
		//.bg -> .rg
		//ARGB 8888 -> B G R A -> B=7:0 aka '1', G=15:8 aka '0'
		//ARGB 8888 -> B G R A -> R=7:0 aka '1', G=15:8 aka '0'

		u8* tex=(u8*)Xe_Surface_LockRect(xe,fog_texture,0,0,0,0,XE_LOCK_WRITE);

		//could just memcpy ;p
		u8* fog_table=(u8*)FOG_TABLE;
		for (int j=0;j<fog_texture->height;j++)
		{
			for (int i=0;i<128;i++)
			{
				const register u32 offs = i << 2;
				tex[offs+0 + j * fog_texture->wpitch]=0;//B
				tex[offs+1 + j * fog_texture->wpitch]=fog_table[(offs+0)^3];//G
				tex[offs+2 + j * fog_texture->wpitch]=fog_table[(offs+1)^3];//R
				tex[offs+3 + j * fog_texture->wpitch]=0;//A
			}
		}

        handle_small_surface(fog_texture,tex);
        
		Xe_Surface_Unlock(xe,fog_texture);
	}
	void SetMVS_Mode(u32 mv_mode,ISP_Modvol ispc)
	{
		if (mv_mode==0)	//normal trigs
		{
			//set states
			Xe_SetZEnable(xe,1);
			Xe_SetStencilWriteMask(xe,3,2);
			Xe_SetStencilFunc(xe,3,XE_CMP_ALWAYS);
			Xe_SetStencilOp(xe,3,-1,XE_STENCILOP_KEEP,XE_STENCILOP_INVERT);
			Xe_SetCullMode(xe,CullMode[ispc.CullMode]);
		}
		else
		{
			//1 (last in) or 2 (last out)
			//each trinagle forms the last of a volume
			if (mv_mode==1)
			{
				//res : old : final 
				//0   : 0      : 00
				//0   : 1      : 01
				//1   : 0      : 01
				//1   : 1      : 01

				//if !=0 -> set to 10
				Xe_SetStencilFunc(xe,3,XE_CMP_LESSEQUAL);
				Xe_SetStencilRef(xe,3,1);
				Xe_SetStencilOp(xe,3,XE_STENCILOP_ZERO,-1,XE_STENCILOP_REPLACE);
			}
			else
			{
				printf("*************************************************************************\n");
				printf("**mv_mode!=1. THIS IS PROLLY BUGGED, REPORT IF NOT ON THE ISSUE TRACKER**\n");
				printf("*************************************************************************\n");
				//res : old : final 
				//0   : 0   : 00
				//0   : 1   : 00
				//1   : 0   : 00
				//1   : 1   : 01

				Xe_SetStencilFunc(xe,3,XE_CMP_GREATER);
				Xe_SetStencilRef(xe,3,2);
				Xe_SetStencilOp(xe,3,XE_STENCILOP_ZERO,-1,XE_STENCILOP_REPLACE);
			}

			//common states :)
			Xe_SetZEnable(xe,0);
			Xe_SetStencilWriteMask(xe,3,3);
			Xe_SetStencilMask(xe,3,3);
		}
	}
	//
	void DoRender()
	{
		dosort=UsingAutoSort();

		bool rtt=false; //gli (FB_W_SOF1 & 0x1000000)!=0;
		
		if (syncPending) Xe_Sync(xe);
        syncPending=false;
    
		Xe_InvalidateState(xe);
//		Xe_SetFillMode(xe,XE_FILL_WIREFRAME,XE_FILL_WIREFRAME);

//		void* ptr;
		if (FrameNumber-rtt_FrameNumber >60)
		{
			rtt_address=0xFFFFFFFF;
		}
		if (rtt)
		{
			rtt_FrameNumber=FrameNumber;
			rtt_address=FB_W_SOF1&VRAM_MASK;
			//write to the 'other' rtt buffer, read from the last writen one
			Xe_SetRenderTarget(xe,rtt_texture[rtt_index^1]);
			//printf("Writing to %d\n",rtt_index^1);
		}
		else
		{
			fb_FrameNumber=FrameNumber;
			Xe_SetRenderTarget(xe,Xe_GetFramebufferSurface(xe));
		}

		// Clear the backbuffer to a blue color
		//All of the screen is allways filled w/ smth , no need to clear the color buffer
		//gives a nice speedup on large resolutions
		Xe_SetScissor(xe,0,0,0,0,0);
		Xe_SetClearColor(xe,0);
		if (rtt || clear_rt==0)
		{
			Xe_ResolveInto(xe,xe->rt,XE_SOURCE_COLOR,XE_CLEAR_DS);
		}
		else
		{
			Xe_ResolveInto(xe,xe->rt,XE_SOURCE_COLOR,XE_CLEAR_DS|XE_CLEAR_COLOR);
			if(clear_rt)
				clear_rt--;
		}

		pvrrc.verts.Finalise();
		u32 sz=pvrrc.verts.used*sizeof(Vertex);

		Vertex * data=(Vertex *)Xe_VB_Lock(xe,vb,0,sz,XE_LOCK_WRITE);
		pvrrc.verts.Copy(data,sz);
		Xe_VB_Unlock(xe,vb);

		//memset(pvrrc.verts.data,0xFEA345FD,pvrrc.verts.size*sizeof(Vertex));

		UpdatePaletteTexure();
		UpdateFogTableTexure();

		// Begin the scene
		{	
			/*
				Pal texture stuff
			*/
			if (pal_texture!=0)
			{
				pal_texture->use_filtering=0;
				Xe_SetTexture(xe,1,pal_texture);
			}

			if (fog_texture!=0)
			{
				fog_texture->use_filtering=0;
				Xe_SetTexture(xe,2,fog_texture);
			}

			//Init stuff
			Xe_SetShader(xe,SHADER_TYPE_VERTEX,sh_vs,0);
			Xe_SetShader(xe,SHADER_TYPE_PIXEL,sh_ps,0);

#define clamp(minv,maxv,x) min(maxv,max(minv,x))
//			float bg=*(float*)&ISP_BACKGND_D; 

#ifdef scale_type_1
			float c0[4]={clamp(0,10000000.0f,pvrrc.invW_min)};
			float c1[4]={clamp(0.0000001f,10000000.0f,pvrrc.invW_max)};
			c0[0]*=0.99f;
			c1[0]*=1.01f;
//			printf("ZMAE: %f %f\n",c0[0],c1[0]);
			Xe_SetVertexShaderConstantF(xe,0,c0,1);
			Xe_SetVertexShaderConstantF(xe,1,c1,1);
#endif
			/*
				Set constants !
			*/

			//VERT and RAM constants
			u8* fog_colvert_bgra=(u8*)&FOG_COL_VERT;
			u8* fog_colram_bgra=(u8*)&FOG_COL_RAM;
			float ps_FOG_COL_VERT[4]={fog_colvert_bgra[2^3]/255.0f,fog_colvert_bgra[1^3]/255.0f,fog_colvert_bgra[0^3]/255.0f,1};
			float ps_FOG_COL_RAM[4]={fog_colram_bgra[2^3]/255.0f,fog_colram_bgra[1^3]/255.0f,fog_colram_bgra[0^3]/255.0f,1};

			Xe_SetPixelShaderConstantF(xe,2,ps_FOG_COL_VERT,1);
			Xe_SetPixelShaderConstantF(xe,3,ps_FOG_COL_RAM,1);

			//Fog density constant
			u8* fog_density=(u8*)&FOG_DENSITY;
			float fog_den_mant=fog_density[1^3]/128.0f;		//bit 7 -> x. bit, so [6:0] -> fraction -> /128
			s32 fog_den_exp=(s8)fog_density[0^3];
			float fog_den_float=fog_den_mant*pow(2.0f,fog_den_exp);

			float ps_FOG_DENSITY[4]= { fog_den_float,0,0,1 };
			Xe_SetPixelShaderConstantF(xe,4,ps_FOG_DENSITY,1);
			/*
				Setup initial render states
			*/
			Xe_SetZEnable(xe,1);

			Xe_SetStreamSource(xe,0,vb,0,sizeof(Vertex));

			//Opaque
			Xe_SetBlendControl(xe,XE_BLEND_ONE,XE_BLENDOP_ADD,XE_BLEND_ZERO,XE_BLEND_ONE,XE_BLENDOP_ADD,XE_BLEND_ZERO);
			Xe_SetAlphaTestEnable(xe,0);

			//Scale values have the sync values
			//adjust em here for FB/AA/Stuff :)
			float scale_x=fb_scale[0];
			float scale_y=fb_scale[1];
			if (VO_CONTROL.pixel_double)
				scale_x*=0.5;
			else
				scale_x*=1;

			float x_scale_coef_aa=2.0;
			if (SCALER_CTL.hscale)
			{
				scale_x*=2;//2x resolution on X (AA)
				x_scale_coef_aa=1.0;
			}
			/*			float yscalef=SCALER_CTL.vscalefactor/1024.0f;
			scale_y*=1/yscalef;
			*/
			if (rtt)
			{
				current_scalef[0]=-(float)(FB_X_CLIP.min*scale_x);
				current_scalef[1]=-(float)(FB_Y_CLIP.min/**scale_y*/);
				current_scalef[2]=(float)(FB_X_CLIP.max+1)*0.5f*scale_x;
				current_scalef[3]=-(float)((FB_Y_CLIP.max+1)*0.5f)/**scale_y*/;
				Xe_SetScissor(xe,0,0,0,0,0);
			}
			else
			{
				current_scalef[0]=res_scale[0]*scale_x;
				current_scalef[1]=res_scale[1]*scale_y;
				current_scalef[2]=res_scale[2]*scale_x;
				current_scalef[3]=res_scale[3]*scale_y;

				//if widescreen mode == keep AR,Render extra
				if (drkpvr_settings.Enhancements.AspectRatioMode==2 && FB_Y_CLIP.min==0 && FB_Y_CLIP.max==479 && FB_X_CLIP.min==0 && FB_X_CLIP.max==639)
				{
					//rendering to frame buffer and not scissoring anything [yes this is a hack to allow widescreen hack]
					Xe_SetScissor(xe,0,0,0,0,0);
				}
				else
				{
					struct XenosSurface *  fb_surf_desc =Xe_GetFramebufferSurface(xe);
					
					int top=(int)(0.5f+(current_scalef[1]/(-current_scalef[3]*2)*fb_surf_desc->height+FB_Y_CLIP.min*fb_surf_desc->height/(-current_scalef[3]*2)));
					int bottom=(int)(0.5f+(current_scalef[1]/(-current_scalef[3]*2)*fb_surf_desc->height+((FB_Y_CLIP.max+1)*fb_surf_desc->height/(-current_scalef[3]*2))));

					int left=(int)(0.5f+(current_scalef[0]/(current_scalef[2]*x_scale_coef_aa)*fb_surf_desc->width+FB_X_CLIP.min*fb_surf_desc->width/(current_scalef[2]*x_scale_coef_aa)));
					int right=(int)(0.5f+(current_scalef[0]/(current_scalef[2]*x_scale_coef_aa)*fb_surf_desc->width+((FB_X_CLIP.max+1)*fb_surf_desc->width/(current_scalef[2]*x_scale_coef_aa))));

					Xe_SetScissor(xe,1,left,top,right,bottom);
				}
			}

			Xe_SetVertexShaderConstantF(xe,2,current_scalef,1);

			float res_align_offs[4]={ (-1.0f/Xe_GetFramebufferSurface(xe)->width)-1.0f,(1.0f/Xe_GetFramebufferSurface(xe)->height)+1.0f };
			Xe_SetVertexShaderConstantF(xe,4,res_align_offs,1);


			//stencil modes
			if (drkpvr_settings.Emulation.ModVolMode==MVM_NormalAndClip)
			{
				Xe_SetStencilEnable(xe,1);
				Xe_SetStencilWriteMask(xe,3,0xff);
				Xe_SetStencilFunc(xe,3,XE_CMP_ALWAYS);
				Xe_SetStencilOp(xe,3,-1,XE_STENCILOP_KEEP,XE_STENCILOP_REPLACE);
				Xe_SetStencilRef(xe,3,0x00);
			}
			else
			{
				Xe_SetStencilEnable(xe,0);
			}
			

			//OPAQUE
			//if (!GetAsyncKeyState(VK_F1))
			{
				if (UseFixedFunction)
				{
					RendPolyParamList<ListType_Opaque,true,false>(pvrrc.global_param_op);
				}
				else
				{
					RendPolyParamList<ListType_Opaque,false,false>(pvrrc.global_param_op);
				}
			}

			//Punch Through
			Xe_SetAlphaTestEnable(xe,1);
			Xe_SetAlphaFunc(xe,XE_CMP_GREATER);
			Xe_SetAlphaRef(xe,(((PT_ALPHA_REF&0xff)>0)?(PT_ALPHA_REF&0xff)-1:0)/255.0f);
			Xe_SetStencilRef(xe,3,0);

			//if (!GetAsyncKeyState(VK_F2))
			{
				if (UseFixedFunction)
				{
					RendPolyParamList<ListType_Punch_Through,true,false>(pvrrc.global_param_pt);
				}
				else
				{
					RendPolyParamList<ListType_Punch_Through,false,false>(pvrrc.global_param_pt);
				}
			}

			
#if 0
			//OP mod vols
			if (drkpvr_settings.Emulation.ModVolMode!=MVM_Off && pvrrc.modtrig.used>0)
			{
				if(drkpvr_settings.Emulation.ModVolMode==MVM_Normal || drkpvr_settings.Emulation.ModVolMode==MVM_NormalAndClip)
				{
					/*
					mode :
					normal trig : flip
					last *in*   : flip, merge*in* &clear from last merge
					last *out*  : flip, merge*out* &clear from last merge
					*/
					//dev->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE); //->BUG on nvdrivers (163 && 169 tested so far)
					Xe_SetAlphaTestEnable(xe,0);
					Xe_SetBlendControl(xe,XE_BLEND_ZERO,XE_BLENDOP_ADD,XE_BLEND_ONE,XE_BLEND_ZERO,XE_BLENDOP_ADD,XE_BLEND_ONE);

					Xe_SetZWrite(xe,0);
					Xe_SetZFunc(xe,XE_CMP_GREATER);
					
					//TODO: Find out what ZPixelShader was supposed to be doing.
					verifyc(dev->SetPixelShader(ZPixelShader));
					Xe_SetStencilEnable(xe,1);

					//we WANT stencil to have all 1's here for bit 1
					//set it as needed here :) -> not realy , we want em 0'd
					
					f32 fsq[] = {-640*8,-480*8,pvrrc.invW_min, -640*8,480*8,pvrrc.invW_min, 640*8,-480*8,pvrrc.invW_min, 640*8,480*8,pvrrc.invW_min};
					/*
					verifyc(dev->SetRenderState(D3DRS_ZENABLE,FALSE));						//Z doesnt matter
					verifyc(dev->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_ALWAYS));			//allways pass
					verifyc(dev->SetRenderState(D3DRS_STENCILWRITEMASK,3));					//write bit 1
					verifyc(dev->SetRenderState(D3DRS_STENCILPASS,D3DSTENCILOP_REPLACE));	//Set to reference (2)
					verifyc(dev->SetRenderState(D3DRS_STENCILREF,2));						//reference value(2)

					verifyc(dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,fsq,3*4));
					*/
					//set correct declaration
					verifyc(dev->SetVertexDeclaration(vdecl_mod));

					u32 mod_base=0;	//cur base
					u32 mod_last=0; //last merge

					u32 cmv_count=(pvrrc.global_param_mvo.used-1);
					//ISP_Modvol
					for (u32 cmv=0;cmv<cmv_count;cmv++)
					{
						u32 sz=pvrrc.global_param_mvo.data[cmv+1].id;
						
						ISP_Modvol ispc=pvrrc.global_param_mvo.data[cmv];
						mod_base=ispc.id;
						sz-=mod_base;
						
						u32 mv_mode = ispc.DepthMode;
						
						//We read from Z buffer, but dont write :)
						verifyc(dev->SetRenderState(D3DRS_ZENABLE,TRUE));
						//enable stenciling, and set bit 1 for mod vols that dont pass the Z test as closed ones (not even count of em)


						if (mv_mode==0)	//normal trigs
						{
							SetMVS_Mode(0,ispc);
							//Render em (counts intersections)
							Xe_DrawPrimitive(xe,XE_PRIMTYPE_TRIANGLELIST,
							verifyc(dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST,sz,pvrrc.modtrig.data+mod_base,3*4));
						}
						else if (mv_mode<3)
						{
							while(sz)
							{
								//merge and clear all the prev. stencil bits
								
								//Count Intersections (last poly)
								SetMVS_Mode(0,ispc);
								verifyc(dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,pvrrc.modtrig.data+mod_base,3*4));
								//Sum the area
								SetMVS_Mode(mv_mode,ispc);
								verifyc(dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST,mod_base-mod_last+1,pvrrc.modtrig.data+mod_last,3*4));

								//update pointers
								mod_last=mod_base+1;
								sz--;
								mod_base++;
							}
						}
						else
						{
							//die("Not supported mv_mode\n");
						}
					}

					//black out any stencil with '1'
					dev->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
					dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
					dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA); 

					dev->SetRenderState(D3DRS_COLORWRITEENABLE,0xF);
					verifyc(dev->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_EQUAL));	//only the odd ones are 'in'
					
					u32 RefValue=0x01;	//'in'

					if (drkpvr_settings.Emulation.ModVolMode==MVM_NormalAndClip)
					{
						RefValue|=0x80;	//stencil volume mask
					}

					verifyc(dev->SetRenderState(D3DRS_STENCILREF,RefValue));	//allways (stencil volume mask && 'in')
					verifyc(dev->SetRenderState(D3DRS_STENCILMASK,RefValue));	//allways (as above)

					verifyc(dev->SetRenderState(D3DRS_STENCILWRITEMASK,0));	//dont write to stencil

					verifyc(dev->SetRenderState(D3DRS_ZENABLE,FALSE));

					verifyc(dev->SetPixelShader(ShadeColPixelShader));
					//render a fullscreen quad

					verifyc(dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,fsq,3*4));

					verifyc(dev->SetRenderState(D3DRS_STENCILENABLE,FALSE));	//turn stencil off ;)
				}
				else if (drkpvr_settings.Emulation.ModVolMode==MVM_Volume)
				{
					//TODO: Find out what ZPixelShader was supposed to be doing.
					verifyc(dev->SetPixelShader(ZPixelShader));
					dev->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
					dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
					dev ->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA); 
					dev->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);

					dev->SetRenderState(D3DRS_ZENABLE,TRUE);
					dev->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
					dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);

					verifyc(dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST,pvrrc.modtrig.used,pvrrc.modtrig.data,3*4));

					
				}

				dev->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);
				dev->SetRenderState(D3DRS_ZENABLE,TRUE);
				dev->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE); 

				dev->SetVertexDeclaration(vdecl);
				dev->SetStreamSource(0,vb,0,sizeof(Vertex));
				//bypass the ps cache and force it to set one :)
				SetPS(0);
				SetPS(1);
			}
#endif

			
//			dev->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
			Xe_SetAlphaFunc(xe,XE_CMP_GREATER);
			Xe_SetAlphaRef(xe,0);
			
			//Disable stencil (we don't support it on transcl. stuff anyway)
			Xe_SetStencilEnable(xe,0);
			//if (!GetAsyncKeyState(VK_F3))
			{
				if (dosort && drkpvr_settings.Emulation.AlphaSortMode==1)
					SortPParams();

				if (dosort && drkpvr_settings.Emulation.AlphaSortMode == 2)
				{
					if (UseFixedFunction)
					{
						SortRendPolyParamList<true>(pvrrc.global_param_tr);
					}
					else
					{
						SortRendPolyParamList<false>(pvrrc.global_param_tr);
					}	
				}
				else
				{
					if (UseFixedFunction)
					{
						if (dosort)
							RendPolyParamList<ListType_Translucent,true,true>(pvrrc.global_param_tr);
						else
							RendPolyParamList<ListType_Translucent,true,false>(pvrrc.global_param_tr);
					}
					else
					{
						if (dosort)
							RendPolyParamList<ListType_Translucent,false,true>(pvrrc.global_param_tr);
						else
							RendPolyParamList<ListType_Translucent,false,false>(pvrrc.global_param_tr);
					}
				}
			}
		}

		if (rtt)
		{
			//swap RTT target for next time !
			rtt_index^=1;
		}

		if(hadTriangles){
			// Present the information rendered to the back buffer to the front buffer (the screen)
			//(xe,Xe_GetFramebufferSurface(xe),XE_SOURCE_COLOR,0);
			Xe_Execute(xe); // render everything in background !
            syncPending=true;
      
			hadTriangles=false;
		}
	}
	
	void ListModes(void(* callback)(u32 w,u32 h,u32 rr))
	{
	}

	#define safe_release(d) {if (d) {(d->Release()==0);d=0;}}
	#define safe_release2(d) {if (d) {verify(d->Release()==0);d=0;}}
	void Medidate_fb()
	{
		int win_width=Xe_GetFramebufferSurface(xe)->width;
		int win_height=Xe_GetFramebufferSurface(xe)->height;
		switch(drkpvr_settings.Video.ResolutionMode)
		{
		case 0:
		default:
			break;

		case 1: //HD(1280x800) or window
			if (win_height*win_width>1024000)
			{
				float rz=sqrtf(1024000.0f/(win_height*win_width));
				win_height = (int)(win_height * rz);
				win_width = (int)(win_width * rz);
			}
			break;
		
		case 2: //640x480
			{
				float ar=((float)win_width/(float)win_height);
				win_width=(s32)(480*ar);
				win_height=480;
			}
			break;
		
		case 3: //Half pixels
			win_height = (int)(win_height / sqrtf(2));
			win_width = (int)(win_width/ sqrtf(2));
			break;

		case 4: //Quarter pixels
			win_height/=2;
			win_width/=2;
			break;
		}
		
		ppar_BackBufferWidth=ppar_BackBufferHeight=-1;

		ppar_BackBufferWidth=win_width;
		ppar_BackBufferHeight=win_height;

		printf("drkpvr: Render target size %dx%d\n",ppar_BackBufferWidth,ppar_BackBufferHeight);

		u32 h=1;
		for(;h<ppar_BackBufferHeight;h<<=1)
			;
		h>>=1;
		u32 w=1;
		for(;w<ppar_BackBufferWidth;w<<=1)
			;
		w>>=1;

		for (int rtid=0;rtid<2;rtid++)
		{
			rtt_texture[rtid]=Xe_CreateTexture(xe,w,h,0,XE_FMT_8888|XE_FMT_ARGB,1);
		}
	}

#if 0
	u32 THREADCALL RenderThead_internal(void* param)
	{
		SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_HIGHEST);

		LARGE_INTEGER freq,InitStart,InitEnd;
		QueryPerformanceFrequency(&freq);
		QueryPerformanceCounter(&InitStart);
		render_restart=false;
		d3d_do_restart=false;
		d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
		// char temp[2][30]; // Unreferenced
		memset(&ppar,0,sizeof(ppar));

		LoadSettings();
		ZBufferMode=drkpvr_settings.Emulation.ZBufferMode;
		bool FZB= SUCCEEDED(d3d9->CheckDeviceFormat(D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,D3DFMT_X8B8G8R8,D3DUSAGE_DEPTHSTENCIL,D3DRTYPE_SURFACE,D3DFMT_D24FS8));
		if (FZB)
			printf("Device Supports D24FS8\n");

		if (ZBufferMode==0 && !FZB)
		{
			printf("Cant use D24FS8, falling back to D24S8+FPE\n");
			ZBufferMode=1;
		}

		D3DCAPS9 caps;
		d3d9->GetDeviceCaps(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,&caps);

		printf("Device caps... VS : %X ; PS : %X\n",caps.VertexShaderVersion,caps.PixelShaderVersion);

		if (caps.VertexShaderVersion<D3DVS_VERSION(1, 0) || FORCE_SW_VERTEX_SHADERS)
		{
			UseSVP=true;
		}
		if (caps.PixelShaderVersion<D3DPS_VERSION(2, 0)|| FORCE_FIXED_FUNCTION)
		{
			UseFixedFunction=true;
		}
		else
		{
			UseFixedFunction=false;
		}

		printf("Will use %s\n",ZBufferModeName[ZBufferMode]);
		printf(UseSVP?"Will use SVP\n":"Will use Vertex Shaders\n");

		if (UseFixedFunction)
		{
			if (drkpvr_settings.Emulation.PaletteMode>1)
			{
				printf("Palette Mode that needs pixel shaders is selected, but no shaders are avaialbe\nReverting to VPT mode\n");
				drkpvr_settings.Emulation.PaletteMode=1;
			}
			if (ZBufferMode==1)
			{
				ZBufferMode=FZB?0:2;
				printf("Fixed function does not support %s, switching to %s\n",ZBufferModeName[1],ZBufferModeName[ZBufferMode]);
			}
		}

		ppar.SwapEffect = D3DSWAPEFFECT_DISCARD;
		ppar.PresentationInterval=drkpvr_settings.Video.VSync?D3DPRESENT_INTERVAL_ONE:D3DPRESENT_INTERVAL_IMMEDIATE;
		ppar.Windowed =   TRUE;

		ppar.EnableAutoDepthStencil=TRUE;
		ppar.AutoDepthStencilFormat = ZBufferMode==0 ? D3DFMT_D24FS8 : D3DFMT_D24S8;

		ppar.MultiSampleType = (D3DMULTISAMPLE_TYPE)drkpvr_settings.Enhancements.MultiSampleCount;
		ppar.MultiSampleQuality = drkpvr_settings.Enhancements.MultiSampleQuality;

		printf("drkpvr: Initialising windowed ");
		printf(" AA:%dx%x\n",ppar.MultiSampleType,ppar.MultiSampleQuality);

		//ppar.Flags |= D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
		
		if (UseSVP || FAILED(d3d9->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,(HWND)emu.GetRenderTarget(),/*D3DCREATE_MULTITHREADED|*/
			D3DCREATE_HARDWARE_VERTEXPROCESSING,&ppar,&dev)))
		{
			if (!UseSVP)
				printf("We had to use SVP after all ...");

			UseSVP=true;
			verifyc(d3d9->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,(HWND)emu.GetRenderTarget(),/*D3DCREATE_MULTITHREADED|*/
				D3DCREATE_SOFTWARE_VERTEXPROCESSING,&ppar,&dev));
		}

		Medidate_fb();

		LPCSTR vsp= D3DXGetVertexShaderProfile(dev);
		if (vsp==0)
		{
			vsp="vs_3_0";
			printf("Strange , D3DXGetVertexShaderProfile(dev) failed , defaulting to \"vs_3_0\"\n");
		}

		if (UseSVP)
			vsp="vs_3_sw";

		printf(UseSVP?"Using SVP/%s\n":"Using Vertex Shaders/%s\n",vsp);
		printf(UseFixedFunction?"Using Fixed Function\n":"Using Pixel Shaders/%s\n",D3DXGetPixelShaderProfile(dev));
		
		vs_macros[0].Definition=ps_macro_numers[ZBufferMode];
		vs_macros[1].Definition=ps_macro_numers[UseFixedFunction?1:0];

		//yay , 20 mb -_- =P
		verifyc(dev->CreateVertexBuffer(20*1024*1024,D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY | (UseSVP?D3DUSAGE_SOFTWAREPROCESSING:0),0,D3DPOOL_DEFAULT,&vb,0));
		
		verifyc(dev->CreateVertexDeclaration(vertelem,&vdecl));
		verifyc(dev->CreateVertexDeclaration(vertelem_mv,&vdecl_mod));
		verifyc(dev->CreateVertexDeclaration(vertelem_osd,&vdecl_osd));
		

		compiled_vs=CompileVS("vs_hlsl.fx","VertexShader_main",vsp,vs_macros);

		Composition.vs=CompileVS("composition.fx","VertexMain",vsp,0);

		verifyc(dev->CreateTexture(640,480,1,D3DUSAGE_DYNAMIC,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&fb_texture8888,0));
		verifyc(fb_texture8888->GetSurfaceLevel(0,&fb_surface8888));

		verifyc(dev->CreateTexture(640,480,1,D3DUSAGE_DYNAMIC,D3DFMT_R5G6B5,D3DPOOL_DEFAULT,&fb_texture565,0));
		verifyc(fb_texture565->GetSurfaceLevel(0,&fb_surface565));

		verifyc(dev->CreateTexture(640,480,1,D3DUSAGE_DYNAMIC,D3DFMT_A1R5G5B5,D3DPOOL_DEFAULT,&fb_texture1555,0));
		verifyc(fb_texture1555->GetSurfaceLevel(0,&fb_surface1555));

		if (!UseFixedFunction)
		{
			verifyc(dev->CreateTexture(16,64,1,D3DUSAGE_DYNAMIC,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&pal_texture,0));
			verifyc(dev->CreateTexture(128,1,1,D3DUSAGE_DYNAMIC,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&fog_texture,0));
			LARGE_INTEGER ps_compile_start,ps_compile_end;
			QueryPerformanceCounter(&ps_compile_start);
			PrecompilePS();
			QueryPerformanceCounter(&ps_compile_end);
			
			printf("Compiling and loading shaders took %.2f ms\n",(ps_compile_end.QuadPart-ps_compile_start.QuadPart)/(freq.QuadPart/1000.0));

#if MODVOL
			ShadeColPixelShader=CompilePS("ps_hlsl.fx","PixelShader_ShadeCol",ps_macros);
			ZPixelShader=CompilePS("ps_hlsl.fx","PixelShader_Z",ps_macros);
#endif
			Composition.ps_DrawA=CompilePS("composition.fx","ps_DrawA",0);
			Composition.ps_DrawFB=CompilePS("composition.fx","ps_DrawFB",0);
			Composition.ps_DrawRGB=CompilePS("composition.fx","ps_DrawRGB",0);
			Composition.ps_DrawRGBA=CompilePS("composition.fx","ps_DrawRGBA",0);
		}

		D3DXCreateFont( dev, 20, 0, FW_BOLD, 0, FALSE, DEFAULT_CHARSET, 
			OUT_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, TEXT("Arial"), &font );

		/*
			Reset Render stuff here
		*/
		clear_rt=5;
		rtt_address=-1;
		rtt_FrameNumber=0;
		d3d_init_done=true;

		QueryPerformanceCounter(&InitEnd);
			
		printf("Initialising 3D Renderer took %.2f ms\n",(InitEnd.QuadPart-InitStart.QuadPart)/(freq.QuadPart/1000.0));
		SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_NORMAL);
		while(1)
		{
			rs.Wait();
			EnterCriticalSection(&d3d_lock);
			if (!running)
				break;
			HRESULT hr;
			hr=dev->TestCooperativeLevel();
			if (FAILED(hr) )
			{
				goto nl;
			}
			//render
			DoRender();
			//if (d3d_do_resize)
			//{
			//	verifyc(dev->Reset(&ppar));
			//	Medidate_fb();
			//	d3d_do_resize=false;
			//}
nl:
			re.Set();
			LeaveCriticalSection(&d3d_lock);
		}
		SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_HIGHEST);

		//*NOTE* we still have the critical section in here ..
		d3d_init_done=false;
		LeaveCriticalSection(&d3d_lock);
		
		// IntelliSense complained... now it should SEE the definition.
		#define safe_release(d) {if (d) {(d->Release()==0);d=0;}}
		
		safe_release(Composition.vs);
		safe_release(Composition.ps_DrawFB);
		safe_release(Composition.ps_DrawA);
		safe_release(Composition.ps_DrawRGB);
		safe_release(Composition.ps_DrawRGBA);

		safe_release(vb);
		safe_release(compiled_vs);

		safe_release(vdecl);
		safe_release(vdecl_mod);
		safe_release(vdecl_osd);
		

		for(int i=0;i<PS_SHADER_COUNT;i++)
			safe_release(compiled_ps[i]);

		safe_release(ShadeColPixelShader);
		safe_release(ZPixelShader);
		
		safe_release(fb_surf);
		safe_release(backbuffer);
		safe_release(rtt_surf[0]);
		safe_release(rtt_surf[1]);
		safe_release(fb_surface8888);
		safe_release(fb_surface1555);
		safe_release(fb_surface565);

		safe_release(fb_texture8888);
		safe_release(fb_texture1555);
		safe_release(fb_texture565);
		safe_release(pal_texture);
		safe_release(fog_texture);
		safe_release(rtt_texture[0]);
		safe_release(rtt_texture[1]);
		safe_release(fb_texture);

		
		safe_release(font);
		safe_release(shader_consts);

		//kill texture cache
		TexCacheList<TextureCacheData>::TexCacheEntry* ptext= TexCache.plast;
		while(ptext)
		{
			ptext->data.Destroy();
			ptext->data.Texture->Release();
			TexCacheList<TextureCacheData>::TexCacheEntry* pprev;
			pprev=ptext->prev;
			TexCache.Remove(ptext);
			//free it !
			delete ptext;
			ptext=pprev;
		}

		safe_release2(dev);
		safe_release2(d3d9);


		#undef safe_release

		return 0;
	}
#endif

	union _ISP_BACKGND_T_type
	{
		struct
		{
#ifdef XENON
			u32 res:3;
			u32 cache_bypass:1;
			u32 shadow:1;
			u32 skip:3;
			u32 tag_address:21;
			u32 tag_offset:3;
#else
			u32 tag_offset:3;
			u32 tag_address:21;
			u32 skip:3;
			u32 shadow:1;
			u32 cache_bypass:1;
			u32 res:3;
#endif
		};
		u32 full;
	};
	union _ISP_BACKGND_D_type
	{
		u32 i;
		f32 f;
	};

	//functions to read data :p
	f32 vrf(u32 addr)
	{
		return *(f32*)&params.vram[vramlock_ConvOffset32toOffset64(addr)];
	}
	u32 vri(u32 addr)
	{
		return *(u32*)&params.vram[vramlock_ConvOffset32toOffset64(addr)];
	}
	void decode_pvr_vertex(u32 base,u32 ptr,Vertex* to);
	u32 old_pal_mode;
    
	void StartRender()
	{
        threaded_wait(true);
        
        //printf("%08x 2\n",PARAM_BASE);

        SetCurrentPVRRC(PARAM_BASE);

		VertexCount+= pvrrc.verts.used;

		render_end_pending_cycles= pvrrc.verts.used*45;
		//if (render_end_pending_cycles<500000)
			render_end_pending_cycles+=500000;

        if (old_pal_mode!=drkpvr_settings.Emulation.PaletteMode)
		{
			//mark pal texures dirty
			TexCacheList<TextureCacheData>::TexCacheEntry* ptext= TexCache.plast;
			while(ptext)
			{
				if ((ptext->data.tcw.PAL.PixelFmt == 5) || (ptext->data.tcw.PAL.PixelFmt == 6))
				{
					ptext->data.dirty=true;
					//Force it to recreate the texture
					if (ptext->data.Texture!=0)
					{
						Xe_DestroyTexture(xe,ptext->data.Texture);
						ptext->data.Texture=0;
					}
				}
				ptext=ptext->prev;
			}
			old_pal_mode=drkpvr_settings.Emulation.PaletteMode;
		}

		//--BG poly
		u32 param_base=PARAM_BASE & 0xF00000;
		_ISP_BACKGND_D_type bg_d; 
		_ISP_BACKGND_T_type bg_t;

		bg_d.i=ISP_BACKGND_D & ~(0xF);
		bg_t.full=ISP_BACKGND_T;
		
        PolyParam bgpp;
		Vertex* cv=BGPoly;

		bool PSVM=(FPU_SHAD_SCALE&0x100)!=0; //double parameters for volumes

		//Get the strip base
		u32 strip_base=(param_base + bg_t.tag_address*4)&0x7FFFFF;	//this is *not* VRAM_MASK on purpose.It fixes naomi bios and quite a few naomi games
																	//i have *no* idea why that happens, they manage to set the render target over there as well
																	//and that area is *not* writen by the games (they instead write the params on 000000 instead of 800000)
																	//could be a h/w bug ? param_base is 400000 and tag is 100000*4
		//Calculate the vertex size
		u32 strip_vs=3 + bg_t.skip;
		u32 strip_vert_num=bg_t.tag_offset;

		if (PSVM && bg_t.shadow)
		{
			strip_vs+=bg_t.skip;//2x the size needed :p
		}
		strip_vs*=4;
		//Get vertex ptr
		u32 vertex_ptr=strip_vert_num*strip_vs+strip_base +3*4;
		//now , all the info is ready :p
		bgpp.isp.full=vri(strip_base);
		bgpp.tsp.full=vri(strip_base+4);
		bgpp.tcw.full=vri(strip_base+8);
		bgpp.count=4;
		bgpp.first=0;
		bgpp.tileclip=0;//disabled ! HA ~

		bgpp.isp.DepthMode=7;// -> this makes things AWFULLY slow .. sometimes
		bgpp.isp.CullMode=0;// -> so that its not culled, or somehow else hiden !
		bgpp.tsp.FogCtrl=2;
		//Set some pcw bits .. i should realy get rid of pcw ..
		bgpp.pcw.UV_16bit=bgpp.isp.UV_16b;
		bgpp.pcw.Gouraud=bgpp.isp.Gouraud;
		bgpp.pcw.Offset=bgpp.isp.Offset;
		bgpp.pcw.Texture=bgpp.isp.Texture;

        pvrrc.global_param_op.data[0]=bgpp;
        
		float scale_x= (SCALER_CTL.hscale) ? 2.f:1.f;	//if AA hack the hacked pos value hacks
		for (int i=0;i<3;i++)
		{
			decode_pvr_vertex(strip_base,vertex_ptr,&cv[i]);
			vertex_ptr+=strip_vs;
		}

		cv[0].x=0;
		cv[0].y=0;
		cv[0].z=bg_d.f;

		cv[1].x=640*scale_x;
		cv[1].y=0;
		cv[1].z=bg_d.f;

		cv[2].x=0;
		cv[2].y=480;
		cv[2].z=bg_d.f;

		cv[3]=cv[2];
		cv[3].x=640*scale_x;
		cv[3].y=480;
		cv[3].z=bg_d.f;
		
		//this is really suboptimal for precition .. but whatever works, right ?
		//(i don't really keep track of min, i just use bg_d)
		//if (pvrrc.invW_min<bg_d.f)
		pvrrc.invW_min=bg_d.f;
		if (pvrrc.invW_max<bg_d.f)
			pvrrc.invW_max=bg_d.f;

		RenderWasStarted=true;

        threaded_call(DoRender);

		FrameCount++;
		
	}

	void Write32BitVram(u32 adr,u32 data)
	{
		*(u32*)&params.vram[vramlock_ConvOffset32toOffset64(adr)]=data;
	}
    
    void HandleLocks()
    {
        for (size_t i=0;i<lock_list.size();i++)
		{
			TextureCacheData* tcd=lock_list[i];
			if (tcd->lock_block==0 && tcd->dirty==false)
				tcd->LockVram();
			
		}
		lock_list.clear();
    }

    void HandleCache()
    {
        TexCacheList<TextureCacheData>::TexCacheEntry* ptext= TexCache.plast;
		while(ptext && ((FrameNumber-ptext->data.LastUsed)>60))
		{
			TexCacheList<TextureCacheData>::TexCacheEntry* pprev;
			pprev=ptext->prev;

			if (drkpvr_settings.Emulation.TexCacheMode==0 || ptext->data.dirty==true)
			{
				ptext->data.Destroy();
				Xe_DestroyTexture(xe,ptext->data.Texture);
				TexCache.Remove(ptext);
				//free it !
				delete ptext;
			}
			ptext=pprev;
		}

    }
    
	void EndRender()
	{
        params.RaiseInterrupt(holly_RENDER_DONE);
        params.RaiseInterrupt(holly_RENDER_DONE_isp);
        params.RaiseInterrupt(holly_RENDER_DONE_vd);

		HandleCache();
        
        if (!RenderWasStarted)
		{
			//printf("Render was not started ..\n");
			return;
		}
		if (!(FB_W_SOF1 & 0x1000000))
		{
			Write32BitVram(FB_W_SOF1,0xDEADC0DE);
			Write32BitVram(FB_W_SOF1+0x280,0xDEADC0DE);
			Write32BitVram(FB_W_SOF1+0x500,0xDEADC0DE);
			Write32BitVram(FB_W_SOF1+0x780,0xDEADC0DE);
			Write32BitVram(FB_W_SOF1+0xA00,0xDEADC0DE);
			Write32BitVram(FB_W_SOF1+0x1400,0xDEADC0DE);
			
			Write32BitVram(FB_W_SOF2,0xDEADC0DE);
			Write32BitVram(FB_W_SOF2+0x280,0xDEADC0DE);
			Write32BitVram(FB_W_SOF2+0x500,0xDEADC0DE);
			Write32BitVram(FB_W_SOF2+0x780,0xDEADC0DE);
			Write32BitVram(FB_W_SOF2+0xA00,0xDEADC0DE);
			Write32BitVram(FB_W_SOF2+0x1400,0xDEADC0DE);
		}
		/*
		if (FB_W_SOF1 & 0x1000000)
		{
			D3DLOCKED_RECT lr;
			HRESULT rv = sysmems->LockRect(&lr,NULL,D3DLOCK_READONLY);
			u32* pixel=(u32*)lr.pBits;
			u32 stride=lr.Pitch/4;

			pixel+=stride*FB_Y_CLIP.min;
			u32 dest=FB_W_SOF1&VRAM_MASK;
			u32 pvr_stride=(FB_W_LINESTRIDE&0xFF)*8;
			for (u32 y=FB_Y_CLIP.min;y<FB_Y_CLIP.max;y++)
			{
				u32 cp=dest;
				for (u32 x=FB_X_CLIP.min;x<FB_X_CLIP.max;x++)
				{
					params.vram[cp]  =pixel[x];
					params.vram[cp+1]=pixel[x]>>16;
					cp+=2;
				}
				pixel+=stride;
				dest+=pvr_stride;
			}

			sysmems->UnlockRect();
		}
*/
		
		u32 old_rev;
		static NDC_WINDOW_RECT nwr;
		bool do_resize;
		static u32 old_res_mode=-1;
		do
		{
			old_rev = resizerq.rev;
			bool same_res=memcmp(&nwr,&resizerq.new_size,sizeof(NDC_WINDOW_RECT))==0 && drkpvr_settings.Video.ResolutionMode==old_res_mode;
			memcpy(&nwr,(void*)&resizerq.new_size,sizeof(NDC_WINDOW_RECT));
			do_resize=resizerq.needs_resize & !same_res;

		} while(old_rev!=resizerq.rev);

		resizerq.needs_resize=false;

		//resize renderer
		if (do_resize)
		{
			
		}

		if (do_resize || render_restart)
		{
			//resize
			old_res_mode=drkpvr_settings.Video.ResolutionMode;
			gfx_do_resize=true;
			//Restart renderer
			render_restart=false;
			gfx_do_restart=true;
		}

		render_end_pending=false;
	}

	__attribute__((aligned(128))) static f32 FaceBaseColor[4];
	__attribute__((aligned(128))) static f32 FaceOffsColor[4];
	__attribute__((aligned(128))) static f32 SFaceBaseColor[4];
	__attribute__((aligned(128))) static f32 SFaceOffsColor[4];

#ifdef MODVOL
	ModTriangle* lmr=0;
	//s32 lmr_count=0;
#endif
	u32 tileclip_val=0;
	struct VertexDecoder
	{

		INLINE
		static void SetTileClip(u32 xmin,u32 ymin,u32 xmax,u32 ymax)
		{
			u32 rv=tileclip_val & 0xF0000000;
			rv|=xmin; //6 bits
			rv|=xmax<<6; //6 bits
			rv|=ymin<<12; //5 bits
			rv|=ymax<<17; //5 bits
			tileclip_val=rv;
		}
		INLINE
		static void TileClipMode(u32 mode)
		{
			tileclip_val=(tileclip_val&(~0xF0000000)) | (mode<<28);
		}
		//list handling
		INLINE
		static void StartList(u32 ListType)
		{
			if (ListType==ListType_Opaque)
				CurrentPPlist=&tarc.global_param_op;
			else if (ListType==ListType_Punch_Through)
				CurrentPPlist=&tarc.global_param_pt;
			else if (ListType==ListType_Translucent)
				CurrentPPlist=&tarc.global_param_tr;
			
			CurrentPP=0;
			vert_reappend=0;
		}
		INLINE
		static void EndList(u32 ListType)
		{
			vert_reappend=0;
			CurrentPP=0;
			CurrentPPlist=0;
			if (ListType==ListType_Opaque_Modifier_Volume)
			{
				ISP_Modvol p;
				p.id=tarc.modtrig.used;
				*tarc.global_param_mvo.Append()=p;
			}
		}

		/*
			if (CurrentPP==0 || CurrentPP->pcw.full!=pp->pcw.full || \
		CurrentPP->tcw.full!=pp->tcw.full || \
		CurrentPP->tsp.full!=pp->tsp.full || \
		CurrentPP->isp.full!=pp->isp.full	) \
		*/
		//Polys  -- update code on sprites if that gets updated too --
#define glob_param_bdc \
		{\
			PolyParam* d_pp =CurrentPPlist->Append(); \
			CurrentPP=d_pp;\
			d_pp->first=tarc.verts.used; \
			d_pp->count=0; \
			vert_reappend=0; \
			d_pp->isp=pp->isp; \
			d_pp->tsp=pp->tsp; \
			d_pp->tcw=pp->tcw; \
			d_pp->pcw=pp->pcw; \
			d_pp->tileclip=tileclip_val;\
		}

#define poly_float_color_(to,a,r,g,b) \
	to[0] = r;	\
	to[1] = g;	\
	to[2] = b;	\
	to[3] = a;

#define sat(x) (x<0?0:x>1?1:x)
#define poly_float_color(to,src) \
	poly_float_color_(to,sat(pp->src##A),sat(pp->src##R),sat(pp->src##G),sat(pp->src##B))

	//poly param handling
	INLINE
		static void fastcall AppendPolyParam0(TA_PolyParam0* pp)
		{
			glob_param_bdc;
		}
		INLINE
		static void fastcall AppendPolyParam1(TA_PolyParam1* pp)
		{
			glob_param_bdc;
			poly_float_color(FaceBaseColor,FaceColor);
		}
		INLINE
		static void fastcall AppendPolyParam2A(TA_PolyParam2A* pp)
		{
			glob_param_bdc;
		}
		INLINE
		static void fastcall AppendPolyParam2B(TA_PolyParam2B* pp)
		{
			poly_float_color(FaceBaseColor,FaceColor);
			poly_float_color(FaceOffsColor,FaceOffset);
		}
		INLINE
		static void fastcall AppendPolyParam3(TA_PolyParam3* pp)
		{
			glob_param_bdc;
		}
		INLINE
		static void fastcall AppendPolyParam4A(TA_PolyParam4A* pp)
		{
			glob_param_bdc;
		}
		INLINE
		static void fastcall AppendPolyParam4B(TA_PolyParam4B* pp)
		{
			poly_float_color(FaceBaseColor,FaceColor0);
		}

		//Poly Strip handling
		//We unite Strips together by dupplicating the [last,first].On odd sized strips
		//a second [first] vert is needed to make sure Culling works fine :)
		INLINE
		static void StartPolyStrip()
		{
			if (vert_reappend)
			{
				Vertex* old=((Vertex*)tarc.verts.ptr);
				if (CurrentPP->count&1)
				{
					Vertex* cv=tarc.verts.Guarantee(4,3);//4
					cv[1].x=cv[0].x=old[-1].x;
					cv[1].y=cv[0].y=old[-1].y;
					cv[1].z=cv[0].z=old[-1].z;
				}
				else
				{
					Vertex* cv=tarc.verts.Guarantee(3,2);//3
					cv[0].x=old[-1].x;//dup prev
					cv[0].y=old[-1].y;//dup prev
					cv[0].z=old[-1].z;//dup prev
				}
				vert_reappend=(Vertex*)tarc.verts.ptr;
			}
		}
		INLINE
		static void EndPolyStrip()
		{
			if (vert_reappend)
			{
				Vertex* vert=vert_reappend;
				vert[-1].x=vert[0].x;
				vert[-1].y=vert[0].y;
				vert[-1].z=vert[0].z;
			}
			vert_reappend=(Vertex*)1;
			CurrentPP->count=tarc.verts.used - CurrentPP->first;
		}

		//Poly Vertex handlers
#ifdef scale_type_1
#define z_update(zv) \
	/*if (tarc.invW_min>zv)\
		tarc.invW_min=zv;*/\
	if (((u32&)zv)<0x41000000 && tarc.invW_max<zv)\
		tarc.invW_max=zv;
#else
	#define z_update(zv)
#endif

	//Append vertex base
#define vert_cvt_base \
	f32 invW=vtx->xyz[2];\
	Vertex* cv=tarc.verts.Append();\
	cv->x=vtx->xyz[0];\
	cv->y=vtx->xyz[1];\
	cv->z=invW;\
	z_update(invW);

	//Resume vertex base (for B part)
#define vert_res_base \
	Vertex* cv=((Vertex*)tarc.verts.ptr)-1;

	//uv 16/32
#define vert_uv_32(u_name,v_name) \
		cv->u	=	(vtx->u_name);\
		cv->v	=	(vtx->v_name);

#define vert_uv_16(u_name,v_name) \
		cv->u	=	f16(vtx->u_name);\
		cv->v	=	f16(vtx->v_name);

	//Color convertions
#ifdef _float_colors_
	#define vert_packed_color_(to,src) \
	{ \
	u32 t=src; \
		to[2]	= unkpack_bgp_to_float[(u8)(t)];t>>=8;\
		to[1]	= unkpack_bgp_to_float[(u8)(t)];t>>=8;\
		to[0]	= unkpack_bgp_to_float[(u8)(t)];t>>=8;\
		to[3]	= unkpack_bgp_to_float[(u8)(t)];	\
	}

	#define vert_float_color_(to,a,r,g,b) \
			to[0] = r;	\
			to[1] = g;	\
			to[2] = b;	\
			to[3] = a;
#else
	#error OLNY floating color is supported for now
#endif

	//Macros to make thins easyer ;)
#define vert_packed_color(to,src) \
	vert_packed_color_(cv->to,vtx->src);

#define vert_float_color(to,src) \
	vert_float_color_(cv->to,vtx->src##A,vtx->src##R,vtx->src##G,vtx->src##B)

	//Intesity handling
#ifdef _HW_INT_
	//Hardware intesinty handling , we just store the int value
	#define vert_int_base(base) \
		cv->base_int = vtx->base;

	#define vert_int_offs(offs) \
		cv->offset_int = vtx->offs;

	#define vert_int_no_base() \
		cv->base_int = 1;

	#define vert_int_no_offs() \
		cv->offset_int = 1;

	#define vert_face_base_color(baseint) \
		vert_float_color_(cv->col,FaceBaseColor[3],FaceBaseColor[0],FaceBaseColor[1],FaceBaseColor[2]);	 \
		vert_int_base(baseint);

	#define vert_face_offs_color(offsint) \
		vert_float_color_(cv->spc,FaceOffsColor[3],FaceOffsColor[0],FaceOffsColor[1],FaceOffsColor[2]);	 \
		vert_int_offs(offsint);
#else
	//Notes:
	//Alpha doesn't get intensity
	//Intesity is clamped before the mul, as well as on face color to work the same as the hardware. [Fixes red dog]

	//Notes:
	//Alpha doesn't get intensity
	//Intesity is clamped before the mul, as well as on face color to work the same as the hardware. [Fixes red dog]

	#define vert_face_base_color(baseint) \
		{ float satint=sat(vtx->baseint); \
		vert_float_color_(cv->col,FaceBaseColor[3],FaceBaseColor[0]*satint,FaceBaseColor[1]*satint,FaceBaseColor[2]*satint); }

	#define vert_face_offs_color(offsint) \
		{ float satint=sat(vtx->offsint); \
		vert_float_color_(cv->spc,FaceOffsColor[3],FaceOffsColor[0]*satint,FaceOffsColor[1]*satint,FaceOffsColor[2]*satint); }

	#define vert_int_no_base()
	#define vert_int_no_offs()
#endif



		//(Non-Textured, Packed Color)
		INLINE
		static void AppendPolyVertex0(TA_Vertex0* vtx)
		{
			vert_cvt_base;

			vert_packed_color(col,BaseCol);

			vert_int_no_base();
		}

		//(Non-Textured, Floating Color)
		INLINE
		static void AppendPolyVertex1(TA_Vertex1* vtx)
		{
			vert_cvt_base;

			vert_float_color(col,Base);

			vert_int_no_base();
		}

		//(Non-Textured, Intensity)
		INLINE
		static void AppendPolyVertex2(TA_Vertex2* vtx)
		{
			vert_cvt_base;
			
			vert_face_base_color(BaseInt);
		}

		//(Textured, Packed Color)
		INLINE
		static void AppendPolyVertex3(TA_Vertex3* vtx)
		{
			vert_cvt_base;
			
			vert_packed_color(col,BaseCol);
			vert_packed_color(spc,OffsCol);

			vert_int_no_base();
			vert_int_no_offs();

			vert_uv_32(u,v);
		}

		//(Textured, Packed Color, 16bit UV)
		INLINE
		static void AppendPolyVertex4(TA_Vertex4* vtx)
		{
			vert_cvt_base;

			vert_packed_color(col,BaseCol);
			vert_packed_color(spc,OffsCol);

			vert_int_no_base();
			vert_int_no_offs();

			vert_uv_16(u,v);
		}

		//(Textured, Floating Color)
		INLINE
		static void AppendPolyVertex5A(TA_Vertex5A* vtx)
		{
			vert_cvt_base;

			//Colors are on B
			vert_int_no_base();
			vert_int_no_offs();

			vert_uv_32(u,v);
		}
		INLINE
		static void AppendPolyVertex5B(TA_Vertex5B* vtx)
		{
			vert_res_base;

			vert_float_color(col,Base);
			vert_float_color(spc,Offs);
		}

		//(Textured, Floating Color, 16bit UV)
		INLINE
		static void AppendPolyVertex6A(TA_Vertex6A* vtx)
		{
			vert_cvt_base;

			//Colors are on B
			vert_int_no_base();
			vert_int_no_offs();

			vert_uv_16(u,v);
		}
		INLINE
		static void AppendPolyVertex6B(TA_Vertex6B* vtx)
		{
			vert_res_base;

			vert_float_color(col,Base);
			vert_float_color(spc,Offs);
		}

		//(Textured, Intensity)
		INLINE
		static void AppendPolyVertex7(TA_Vertex7* vtx)
		{
			vert_cvt_base;

			vert_face_base_color(BaseInt);
			vert_face_offs_color(OffsInt);

			vert_uv_32(u,v);
		}

		//(Textured, Intensity, 16bit UV)
		INLINE
		static void AppendPolyVertex8(TA_Vertex8* vtx)
		{
			vert_cvt_base;

			vert_face_base_color(BaseInt);
			vert_face_offs_color(OffsInt);

			vert_uv_16(u,v);
			
		}

		//(Non-Textured, Packed Color, with Two Volumes)
		INLINE
		static void AppendPolyVertex9(TA_Vertex9* vtx)
		{
			vert_cvt_base;

			vert_packed_color(col,BaseCol0);

			vert_int_no_base();
		}

		//(Non-Textured, Intensity,	with Two Volumes)
		INLINE
		static void AppendPolyVertex10(TA_Vertex10* vtx)
		{
			vert_cvt_base;
			
			vert_face_base_color(BaseInt0);
		}

		//(Textured, Packed Color,	with Two Volumes)	
		INLINE
		static void AppendPolyVertex11A(TA_Vertex11A* vtx)
		{
			vert_cvt_base;

			vert_packed_color(col,BaseCol0);
			vert_packed_color(spc,OffsCol0);

			vert_int_no_base();
			vert_int_no_offs();

			vert_uv_32(u0,v0);
		}
		INLINE
		static void AppendPolyVertex11B(TA_Vertex11B* vtx)
		{
			vert_res_base;

		}

		//(Textured, Packed Color, 16bit UV, with Two Volumes)
		INLINE
		static void AppendPolyVertex12A(TA_Vertex12A* vtx)
		{
			vert_cvt_base;

			vert_packed_color(col,BaseCol0);
			vert_packed_color(spc,OffsCol0);

			vert_int_no_base();
			vert_int_no_offs();

			vert_uv_16(u0,v0);
		}
		INLINE
		static void AppendPolyVertex12B(TA_Vertex12B* vtx)
		{
			vert_res_base;

		}

		//(Textured, Intensity,	with Two Volumes)
		INLINE
		static void AppendPolyVertex13A(TA_Vertex13A* vtx)
		{
			vert_cvt_base;

			vert_face_base_color(BaseInt0);
			vert_face_offs_color(OffsInt0);

			vert_uv_32(u0,v0);
		}
		INLINE
		static void AppendPolyVertex13B(TA_Vertex13B* vtx)
		{
			vert_res_base;

		}

		//(Textured, Intensity, 16bit UV, with Two Volumes)
		INLINE
		static void AppendPolyVertex14A(TA_Vertex14A* vtx)
		{
			vert_cvt_base;

			vert_face_base_color(BaseInt0);
			vert_face_offs_color(OffsInt0);

			vert_uv_16(u0,v0);
		}
		INLINE
		static void AppendPolyVertex14B(TA_Vertex14B* vtx)
		{
			vert_res_base;

		}

		//Sprites
		INLINE
		static void AppendSpriteParam(TA_SpriteParam* spr)
		{
			//printf("Sprite\n");
			PolyParam* d_pp =CurrentPPlist->Append(); 
			CurrentPP=d_pp;
			d_pp->first=tarc.verts.used; 
			d_pp->count=0; 
			vert_reappend=0; 
			d_pp->isp=spr->isp; 
			d_pp->tsp=spr->tsp; 
			d_pp->tcw=spr->tcw; 
			d_pp->pcw=spr->pcw; 
			d_pp->tileclip=tileclip_val;

			vert_packed_color_(SFaceBaseColor,spr->BaseCol);
			vert_packed_color_(SFaceOffsColor,spr->OffsCol);
		}

#define append_sprite(indx) \
	vert_float_color_(cv[indx].col,SFaceBaseColor[3],SFaceBaseColor[0],SFaceBaseColor[1],SFaceBaseColor[2])\
	vert_float_color_(cv[indx].spc,SFaceOffsColor[3],SFaceOffsColor[0],SFaceOffsColor[1],SFaceOffsColor[2])
	//cv[indx].offset_int=1;

#define append_sprite_yz(indx,set,st2) \
	cv[indx].y=sv->y##set; \
	cv[indx].z=sv->z##st2; \
	z_update(sv->z##st2);

#define sprite_uv(indx,u_name,v_name) \
		cv[indx].u	=	f16(sv->u_name);\
		cv[indx].v	=	f16(sv->v_name);
		//Sprite Vertex Handlers
		INLINE
		static void AppendSpriteVertexA(TA_Sprite1A* sv)
		{
			if (CurrentPP->count)
			{
				Vertex* old=((Vertex*)tarc.verts.ptr);
				Vertex* cv=tarc.verts.Guarantee(6,2);
				cv[0].x=old[-1].x;//dup prev
				cv[0].y=old[-1].y;//dup prev
				cv[0].z=old[-1].z;//dup prev
				vert_reappend=(Vertex*)tarc.verts.ptr;
			}

			Vertex* cv = tarc.verts.Append(4);
			
			//Fill static stuff
			append_sprite(0);
			append_sprite(1);
			append_sprite(2);
			append_sprite(3);

			cv[2].x=sv->x0;
			cv[2].y=sv->y0;
			cv[2].z=sv->z0;
			z_update(sv->z0);

			cv[3].x=sv->x1;
			cv[3].y=sv->y1;
			cv[3].z=sv->z1;
			z_update(sv->z1);

			cv[1].x=sv->x2;
		}
		static void CaclulateSpritePlane(Vertex* base)
		{
			const Vertex& A=base[2];
			const Vertex& B=base[3];
			const Vertex& C=base[1];
			      Vertex& P=base[0];
			//Vector AB = B-A;
            //Vector AC = C-A;
            //Vector AP = P-A;
			float AC_x=C.x-A.x,AC_y=C.y-A.y,AC_z=C.z-A.z,
				  AB_x=B.x-A.x,AB_y=B.y-A.y,AB_z=B.z-A.z,
				  AP_x=P.x-A.x,AP_y=P.y-A.y;

			float P_y=P.y,P_x=P.x,P_z=P.z,A_x=A.x,A_y=A.y,A_z=A.z;

			float AB_v=B.v-A.v,AB_u=B.u-A.u,
				  AC_v=C.v-A.v,AC_u=C.u-A.u;

			float /*P_v,P_u,*/A_v=A.v,A_u=A.u;

            float k3 = (AC_x * AB_y - AC_y * AB_x);
 
            if (k3 == 0)
            {
                //throw new Exception("WTF?!");
            }
 
            float k2 = (AP_x * AB_y - AP_y * AB_x) / k3;
 
            float k1 = 0;
 
            if (AB_x == 0)
            {
                //if (AB_y == 0)
				//	;
                //    //throw new Exception("WTF?!");
 
                k1 = (P_y - A_y - k2 * AC_y) / AB_y;
            }
            else
            {
                k1 = (P_x - A_x - k2 * AC_x) / AB_x;
            }
 
			P.z = A_z + k1 * AB_z + k2 * AC_z;
            P.u = A_u + k1 * AB_u + k2 * AC_u;
			P.v = A_v + k1 * AB_v + k2 * AC_v;
		}
		INLINE
		static void AppendSpriteVertexB(TA_Sprite1B* sv)
		{
			vert_res_base;
			cv-=3;

			cv[1].y=sv->y2;
			cv[1].z=sv->z2;
			z_update(sv->z2);

			cv[0].x=sv->x3;
			cv[0].y=sv->y3;
			//cv[0].z=sv->z2; //temp , gota calc. 4th Z properly :p


			sprite_uv(2, u0,v0);
			sprite_uv(3, u1,v1);
			sprite_uv(1, u2,v2);
			//sprite_uv(0, u0,v2);//or sprite_uv(u2,v0); ?

			CaclulateSpritePlane(cv);

			z_update(cv[0].z);

			if (CurrentPP->count)
			{
				Vertex* vert=vert_reappend;
				vert[-1].x=vert[0].x;
				vert[-1].y=vert[0].y;
				vert[-1].z=vert[0].z;
				CurrentPP->count+=2;
			}
			
			CurrentPP->count+=4;
		}

		//ModVolumes

		//Mod Volume Vertex handlers
		static void StartModVol(TA_ModVolParam* param)
		{
			if (TileAccel.CurrentList!=ListType_Opaque_Modifier_Volume)
				return;
			ISP_Modvol p;
			p.full=param->isp.full;
			p.VolumeLast=param->pcw.Volume;
			p.id=tarc.modtrig.used;

			*tarc.global_param_mvo.Append()=p;
			/*
			printf("MOD VOL %d - 0x%08X 0x%08X 0x%08X \n",tarc.modtrig.used,param->pcw.Volume,param->isp.DepthMode,param->isp.CullMode);
			
			if (param->pcw.Volume || param->isp.DepthMode)
			{
				//if (lmr_count)
				//{
					*tarc.modsz.Append()=lmr_count+1;
					lmr_count=-1;
				//}
			}
			*/
		}
		INLINE
		static void AppendModVolVertexA(TA_ModVolA* mvv)
		{
		#ifdef MODVOL
			if (TileAccel.CurrentList!=ListType_Opaque_Modifier_Volume)
				return;
			lmr=tarc.modtrig.Append();

			lmr->x0=mvv->x0;
			lmr->y0=mvv->y0;
			lmr->z0=mvv->z0;
			lmr->x1=mvv->x1;
			lmr->y1=mvv->y1;
			lmr->z1=mvv->z1;
			lmr->x2=mvv->x2;

			z_update(mvv->z1);
			z_update(mvv->z0);
			//lmr_count++;
		#endif	
		}
		INLINE
		static void AppendModVolVertexB(TA_ModVolB* mvv)
		{
		#ifdef MODVOL
			if (TileAccel.CurrentList!=ListType_Opaque_Modifier_Volume)
				return;
			lmr->y2=mvv->y2;
			lmr->z2=mvv->z2;
			z_update(mvv->z2);
		#endif
		}

		//Misc
		INLINE
		static void ListCont()
		{
			//printf("LC : TA OL base = 0x%X\n",TA_OL_BASE);
			SetCurrentTARC(TA_ISP_BASE);
		}
		INLINE
		static void ListInit()
		{
			//printf("LI : TA OL base = 0x%X\n",TA_OL_BASE);
			SetCurrentTARC(TA_ISP_BASE);
			tarc.Clear();

			//allocate storage for BG poly
			tarc.global_param_op.Append();
			BGPoly=tarc.verts.Append(4);
		}
		INLINE
		static void SoftReset()
		{
		}
	};

	//decode a vertex in the native pvr format
	//used for bg poly
	void decode_pvr_vertex(u32 base,u32 ptr,Vertex* cv)
	{
		//ISP
		//TSP
		//TCW
		ISP_TSP isp;
		TSP tsp;
		TCW tcw;

		isp.full=vri(base);
		tsp.full=vri(base+4);
		tcw.full=vri(base+8);

		//XYZ
		//UV
		//Base Col
		//Offset Col

		//XYZ are _allways_ there :)
		cv->x=vrf(ptr);ptr+=4;
		cv->y=vrf(ptr);ptr+=4;
		cv->z=vrf(ptr);ptr+=4;

		if (isp.Texture)
		{	//Do texture , if any
			if (isp.UV_16b)
			{
				u32 uv=vri(ptr);
				cv->u	=	f16((u16)uv);
				cv->v	=	f16((u16)(uv>>16));
				ptr+=4;
			}
			else
			{
				cv->u=vrf(ptr);ptr+=4;
				cv->v=vrf(ptr);ptr+=4;
			}
		}

		//Color
		u32 col=vri(ptr);ptr+=4;
		vert_packed_color_(cv->col,col);
		if (isp.Offset)
		{
			//Intesity color (can be missing too ;p)
			u32 col=vri(ptr);ptr+=4;
			vert_packed_color_(cv->spc,col);
		}
	}


	//--------------------------------------------------------------------------------------
	// Create Direct3D device and swap chain
	//--------------------------------------------------------------------------------------
	void InitDevice()
	{
#ifndef USE_GUI
		xe = &_xe;
			/* initialize the GPU */
		Xe_Init(xe);
#else
		xe = GetVideoDevice();
#endif
			/* create a render target (the framebuffer) */
		struct XenosSurface *fb = Xe_GetFramebufferSurface(xe);
		Xe_SetRenderTarget(xe, fb);
		Xe_SetClearColor(xe,0);

		sh_ps = Xe_LoadShaderFromMemory(xe, inc_ps);
		Xe_InstantiateShader(xe, sh_ps, 0);

		sh_vs = Xe_LoadShaderFromMemory(xe, inc_vs);
		Xe_InstantiateShader(xe, sh_vs, 0);
		Xe_ShaderApplyVFetchPatches(xe, sh_vs, 0, &VertexBufferFormat);

		vb = Xe_CreateVertexBuffer(xe,MAX_VERTEX_COUNT*sizeof(Vertex));

		fog_texture=Xe_CreateTexture(xe,128,1,0,XE_FMT_8888|XE_FMT_ARGB,0);
		pal_texture=Xe_CreateTexture(xe,16,64,0,XE_FMT_8888|XE_FMT_ARGB,0);
		
#ifndef USE_GUI
		edram_init(xe);
#endif
		
		int i;
		for(i=0;i<10;++i){
			Xe_Resolve(xe);
			Xe_Sync(xe);
		}
	}


	//--------------------------------------------------------------------------------------
	// Clean up the objects we've created
	//--------------------------------------------------------------------------------------
	void CleanupDevice()
	{
	}
	
	bool InitRenderer()
	{
		for (u32 i=0;i<256;i++)
		{
			unkpack_bgp_to_float[i]=i/255.0f;
		}
		for (u32 i=0;i<rcnt.size();i++)
		{
			rcnt[i].Free();
		}
		rcnt.clear();
		tarc.Address=0xFFFFFFFF;
		tarc.Init();
		//pvrrc needs no init , it is ALLWAYS copied from a valid tarc :)

		InitDevice();

		return TileAccel.Init();
	}

	void TermRenderer()
	{
		for (u32 i=0;i<rcnt.size();i++)
		{
			rcnt[i].Free();
		}
		rcnt.clear();

		CleanupDevice();

		TileAccel.Term();
		//free all textures
		verify(TexCache.pfirst==0);
	}

	void ResetRenderer(bool Manual)
	{
		TileAccel.Reset(Manual);
		VertexCount=0;
		FrameCount=0;
	}

	void ListCont()
	{
		TileAccel.ListCont();
	}
	void ListInit()
	{
		TileAccel.ListInit();
	}
	void SoftReset()
	{
		TileAccel.SoftReset();
	}
#endif
