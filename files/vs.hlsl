//Vertex Shader :)
//_HW_INT_ -> Intesity needs to be calculated , the factors are stored in z/w of texcoords
//scale_type_1 -> use old way of scaling , needs W_min/W_max to have valid values
//RES_X -> x resolution
//RES_Y -> y resolution
//ZBufferMode -> z buffer mode :p
//ZBufferMode : 0 -> fp fixup
//ZBufferMode : 1 -> nothing
//ZBufferMode : 2 -> non linear rescale
//ZBufferMode : 3 -> non linear rescale
//ZBufferMode : 4 -> linear rescale
//FixedFunction : 0 if ps, 1 if fixed function

#ifndef FLT_MIN
#define FLT_MIN 1.17549435e-38f
#endif

#ifndef FLT_MAX
#define FLT_MAX 3.40282347e+38f
#endif

#define ZBufferMode 4

struct vertex_in 
{
	float3 pos : POSITION;
	float4 col : COLOR0;
	float4 spc : COLOR1; 
	float2 uv : TEXCOORD0; 
};

struct vertex_out
{ 
	float4 pos : SV_POSITION;
	
	float4 uv : TEXCOORD0;
	float4 col : TEXCOORD1;
	float4 spc : TEXCOORD2;
};

float W_min: register(c0);
float W_max: register(c1);
float4 res_scale: register(c2);
float4 texture_size:  register(c3);
float4 res_align_offset:  register(c4); // float2((-1.0/res.x)-1.0,(1.0/res.y)+1.0);

float fdecp(float flt,out float e)
{
	float lg2=log2(flt);	//ie , 2.5
	float frc=frac(lg2);	//ie , 0.5
	e=lg2-frc;				//ie , 2.5-0.5=2 (exp)
	return pow(2,frc);		//2^0.5 (manitsa)
}

float CompressZ1(float w)
{
	float e,m;
	m=frexp(w,e);
	e=clamp(e-32,-127,0);
	return ldexp(m,e);
}

float CompressZ2(float w)
{
	float x;
	float y=fdecp(w,x);
	x=clamp(x-16,-63,0);	//s6e18, max : 2^16*(2^18-1)/2(^18) , min : 2^-47*(2^18-1)/2(^18)
	x+=62;					//bias to positive, +1 more is done by the add below.x_max =62,x_min = -1 (63;0)
	//y						//mantissa bits, allways in [1..2) range as 0 is not a valid input :)
	return (x+y)/64.0f;		//Combine and save the exp + mantissa at the mantissa field.Min value is 0 (-1+1), max value is 63 +(2^18-1)/2(^18).
							//Normalised by 64 so that it falls in the [0..1) range :)
}

float CompressZ3(float w)
{
	if (w<128)
	{
		return 0.875*(w/128);
	}
	else if (w<8192)
	{
		return  0.875 + 0.0625*(w/8192);
	}
	else
	{
		return (0.875 + 0.0625) + 0.0625*(w/FLT_MAX);
	}
}

#define Z4_ZONE1I 4
#define Z4_ZONE2I 8192
#define Z4_ZONE3I FLT_MAX

#define Z4_ZONE1O 0.95
#define Z4_ZONE2O 0.045
#define Z4_ZONE3O 0.005

float CompressZ4(float w)
{
	if (w<Z4_ZONE1I)
	{
		return Z4_ZONE1O*(w/Z4_ZONE1I);
	}
	else if (w<Z4_ZONE2I)
	{
		return  Z4_ZONE1O + Z4_ZONE2O*(w/Z4_ZONE2I);
	}
	else
	{
		return (Z4_ZONE1O + Z4_ZONE2O) + Z4_ZONE3O*(w/Z4_ZONE3I);
	}
}

vertex_out main(in vertex_in vin)
{
	vertex_out vo = (vertex_out)0;
	vo.pos.xy=vin.pos.xy+res_scale.xy;
	vo.pos.xy/=res_scale.zw;

	vo.pos.xy+=res_align_offset.xy;

	//vin.uv=(floor(vin.uv*texture_size.xy)+float2(0.5f,0.5f))*texture_size.zw;
	//vin.uv=(vin.uv*(texture_size.xy-float2(1,1))+float2(0.5f,0.5f))*texture_size.zw;
	vo.uv.xy=vin.uv*vin.pos.z;

	vo.col=saturate(vin.col)*vin.pos.z;
	vo.spc=saturate(vin.spc)*vin.pos.z;

	vo.uv.w=vin.pos.z;

	//I need to do smth about fixed function for this one
	if (! (vin.pos.z<FLT_MAX && vin.pos.z>0))
		vo.uv.z=-1;
	else
		vo.uv.z=0;

	#if ZBufferMode==0
		vo.pos.z=CompressZ1(vin.pos.z);
	#elif ZBufferMode==1
		vo.pos.z=0;
	#elif ZBufferMode==2
		//vo.pos.z=1-1/(1+vin.pos.z);
		vo.pos.z=CompressZ2(vin.pos.z);
	#elif ZBufferMode==3
		//vo.pos.z=1-1/(1+vin.pos.z);
		vo.pos.z=CompressZ3(vin.pos.z);
	#elif ZBufferMode==4
		vo.pos.z=CompressZ4(vin.pos.z);
	#endif

	vo.pos.z=1.0-vo.pos.z;
	vo.pos.w=1;

	return vo;
}