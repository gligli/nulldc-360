
#include "audiostream_rif.h"
#include "audiostream.h"

#include <xenon_sound/sound.h>

#define MAX_UNPLAYED 16384

#define BUFFER_SIZE 65536
static u8 buffer[BUFFER_SIZE];
static u8 buffer48[BUFFER_SIZE];

static s16 prevLastSample[2]={0,0};
// resamples pStereoSamples (taken from http://pcsx2.googlecode.com/svn/trunk/plugins/zerospu2/zerospu2.cpp)
void ResampleLinear(s16* pStereoSamples, s32 oldsamples, s16* pNewSamples, s32 newsamples)
{
		s32 newsampL, newsampR;
		s32 i;
		
		for (i = 0; i < newsamples; ++i)
        {
                s32 io = i * oldsamples;
                s32 old = io / newsamples;
                s32 rem = io - old * newsamples;

                old *= 2;
				//printf("%d %d\n",old,oldsamples);
				if (old==0){
					newsampL = prevLastSample[0] * (newsamples - rem) + pStereoSamples[0] * rem;
					newsampR = prevLastSample[1] * (newsamples - rem) + pStereoSamples[1] * rem;
				}else{
					newsampL = pStereoSamples[old-2] * (newsamples - rem) + pStereoSamples[old] * rem;
					newsampR = pStereoSamples[old-1] * (newsamples - rem) + pStereoSamples[old+1] * rem;
				}
                pNewSamples[2 * i] = newsampL / newsamples;
                pNewSamples[2 * i + 1] = newsampR / newsamples;
        }

		prevLastSample[0]=pStereoSamples[oldsamples*2-2];
		prevLastSample[1]=pStereoSamples[oldsamples*2-1];
}

void xenon_InitAudio()
{
   
}

void xenon_TermAudio()
{
}


int bufpos=0;
int sound_inited=0;
    
void xenon_WriteSample(s16 r,s16 l)
{
    if(!sound_inited)
    {
        xenon_sound_init();
        sound_inited=1;
    }
    
    int bufs=441*4;
    int buf48s=(bufs*48000/44100)&~1;
    
    
    *((s16*)&buffer[bufpos])=r; bufpos+=2;
    *((s16*)&buffer[bufpos])=l; bufpos+=2;
    
    if (bufpos>=bufs)
    {
		ResampleLinear((s16 *)buffer,bufs/4,(s16 *)buffer48,buf48s/4);

        int i;
        for(i=0;i<buf48s/2;++i) ((s16*)buffer48)[i]=bswap_16(((s16*)buffer48)[i]);

        while(xenon_sound_get_unplayed()>MAX_UNPLAYED);
  
        xenon_sound_submit(buffer48,buf48s);
        
        bufpos=0;
    }
}
