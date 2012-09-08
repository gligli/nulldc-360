sampler tex0;

float4x4 matWVP : register(c0);

float alpha : register(c8);

struct _VSIN
{
    float4 pos: POSITION;
    float4 col: COLOR0;
	float4 padding1: COLOR1;
    float2 uv0: TEXCOORD0;
	
};

struct _VSOUT
{
  float4 pos: POSITION;
  float4 col: COLOR;
  float2 uv0: TEXCOORD0;
};

struct _PSIN
{
    float4 col : COLOR0;
    float2 uv: TEXCOORD0;
};

_VSOUT VSmain(_VSIN In )
{
    _VSOUT Out;

    Out.pos = mul(matWVP,In.pos);
	//Out.pos = In.pos;
	
	Out.pos.y = -Out.pos.y;

    Out.col = In.col;
    Out.uv0 = In.uv0;
	
    return Out;
}

float4 psTC(_PSIN data): COLOR {
    float4 Color;

    Color = tex2D( tex0, data.uv.xy) * data.col;

    return  Color;
}

float4 psT(_PSIN data): COLOR {
    float4 Color;

    Color = tex2D( tex0, data.uv.xy);
/*	
	Color.a=1.0f;
	Color.r = 0.5f;
	Color.b = 1.f;
	Color.g = 0.f;
	*/
    return  Color * data.col ;
}

float4 psC(_PSIN data): COLOR {
    float4 Color;

    Color = data.col;
	
    return  Color;
}

float4 psSnes(_PSIN data): COLOR {
    return   tex2D( tex0, data.uv.xy);
}