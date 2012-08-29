#include "xenon_audiostream.h"

#include <xenon_sound/sound.h>
#include <time/time.h>

#define MAX_UNPLAYED 16384
#define BUFFER_SIZE 65536

static int bufs=441;
static int buf48s=bufs*48000/44100;
static s16 buffer[BUFFER_SIZE];
static s16 buffer48[BUFFER_SIZE];
static int bufpos=0;

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
    
void xenon_WriteSample(s16 r,s16 l)
{
    buffer[bufpos++]=r;
    buffer[bufpos++]=l;
    
    if (bufpos>=bufs)
    {
		ResampleLinear(buffer,bufs/2,buffer48,buf48s/2);

        int i;
        for(i=0;i<buf48s;++i) buffer48[i]=bswap_16(buffer48[i]);

//        while(xenon_sound_get_unplayed()>MAX_UNPLAYED);
  
        xenon_sound_submit(buffer48,buf48s*2);
        
        bufpos=0;
    }
}
