/*
	sh4 base core
	most of it is (very) old
	could use many cleanups, lets hope someone does them
*/

//this file contains ALL register to register full moves
//

//stc GBR,<REG_N>               
rsh4op(i0000_nnnn_0001_0010)
{
	u32 n = GetN(op);

	r[n] = gbr;
}

//stc VBR,<REG_N>               
rsh4op(i0000_nnnn_0010_0010)
{
	u32 n = GetN(op);

	r[n] = vbr;
}

//stc SSR,<REG_N>               
rsh4op(i0000_nnnn_0011_0010)
{
	u32 n = GetN(op);

	r[n] = ssr;
}

//stc SGR,<REG_N>               
rsh4op(i0000_nnnn_0011_1010)
{
	u32 n = GetN(op);
	r[n] = sgr;
}

//stc SPC,<REG_N>               
rsh4op(i0000_nnnn_0100_0010)
{
	u32 n = GetN(op);

	r[n] = spc;
}

//stc RM_BANK,<REG_N>           
rsh4op(i0000_nnnn_1mmm_0010)
{
	u32 n = GetN(op);
	u32 m = GetM(op) & 0x7;

	r[n] = r_bank[m];
}

//sts FPUL,<REG_N>              
rsh4op(i0000_nnnn_0101_1010)
{
	u32 n = GetN(op);

	r[n] = fpul;
}

//stc DBR,<REG_N>             
rsh4op(i0000_nnnn_1111_1010)
{
	u32 n = GetN(op);

	r[n] = dbr;
}

//sts MACH,<REG_N>              
rsh4op(i0000_nnnn_0000_1010)
{
	u32 n = GetN(op);

	r[n] = mac.h;
}

//sts MACL,<REG_N>              
rsh4op(i0000_nnnn_0001_1010)
{
	u32 n = GetN(op);

	r[n]=mac.l; 
} 

//sts PR,<REG_N>                
rsh4op(i0000_nnnn_0010_1010)
{
	u32 n = GetN(op);

	r[n] = pr;
} 

//lds <REG_N>,MACH              
rsh4op(i0100_nnnn_0000_1010)
{
	u32 n = GetN(op);

	mac.h = r[n];
}


//lds <REG_N>,MACL              
rsh4op(i0100_nnnn_0001_1010)
{
	u32 n = GetN(op);

	mac.l = r[n];
}

//lds <REG_N>,PR                
rsh4op(i0100_nnnn_0010_1010)
{
	u32 n = GetN(op);
	pr = r[n];
}

//lds <REG_N>,FPUL              
rsh4op(i0100_nnnn_0101_1010)
{
	u32 n = GetN(op);

	fpul = r[n];
}

//ldc <REG_N>,DBR                
rsh4op(i0100_nnnn_1111_1010)
{
	u32 n = GetN(op);

	dbr = r[n];
}

//ldc <REG_N>,GBR               
rsh4op(i0100_nnnn_0001_1110)
{
	u32 n = GetN(op);

	gbr = r[n];
}

//ldc <REG_N>,VBR               
rsh4op(i0100_nnnn_0010_1110)
{
	u32 n = GetN(op);

	vbr = r[n];
}

//ldc <REG_N>,SSR               
rsh4op(i0100_nnnn_0011_1110)
{
	u32 n = GetN(op);

	ssr = r[n];
}

//ldc <REG_N>,SGR               
rsh4op(i0100_nnnn_0011_1010)
{
	u32 n = GetN(op);

	sgr = r[n];
}

//ldc <REG_N>,SPC               
rsh4op(i0100_nnnn_0100_1110)
{
	u32 n = GetN(op);

	spc = r[n];
}

//ldc <REG_N>,RM_BANK           
rsh4op(i0100_nnnn_1mmm_1110)
{
	u32 n = GetN(op);
	u32 m = GetM(op) & 7;

	r_bank[m] = r[n];
}

//mov <REG_M>,<REG_N>           
rsh4op(i0110_nnnn_mmmm_0011)
{
	u32 n = GetN(op);
	u32 m = GetM(op);

	r[n] = r[m];
}

//
//Cxxx

// mova @(<disp>,PC),R0          
rsh4op(i1100_0111_iiii_iiii)
{
	u32 disp = GetImm8(op);

	r[0] = (pc&0xFFFFFFFC)+4+(disp<<2);
}

//
// Exxx

// mov #<imm>,<REG_N>
 rsh4op(i1110_nnnn_iiii_iiii)
{
	u32 n = GetN(op);

	r[n] = (u32)(s32)(s8)(GetImm8(op));
}

//clrmac                        
rsh4op(i0000_0000_0010_1000)
{
	mac.l=(u32)0;
	mac.h=(u32)0;
}