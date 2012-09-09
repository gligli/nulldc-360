#pragma once
#include "types.h"

extern bool TBP_Enabled;
void init_Profiler(void* param);
void term_Profiler();

void start_Profiler();
void stop_Profiler();

u64 CycleDiff();

int percent(int tick, int total);
int effsceas(int tick, int cycleDif);

enum
{		
	GFXC = 0,
	AICAC,
	ARMC,
	GDROMC,
	MAPLEC,
	DYNAC,
	DYNA_LOOPC,
	MAINC,
	RESTC
};

enum
{
	TICKS = 0, 	
	PERCENT,	
	EFFSCEAS,	
};

struct prof_stats
{	
	int avg_count[9][3];
	int max_count[9][3];
	int avg_counter;
};

struct prof_info
{						
	int current_count[9];	
	int total_tc; //total tics		

	/*
	int gfx_tick_count;			//on gfx dll
	int aica_tick_count;		//on aica dll
	int arm_tick_count;			//on arm dll
	int gdrom_tick_count;		//on gdrom dll
	int maple_tick_count;		//on maple dll
	int dyna_tick_count;		//on dynarec mem
	int dyna_loop_tick_count;	//on dynarec loop exe
	int main_tick_count;		//on main exe
	int rest_tick_count;		//dunno where :p
	*/	

	u64 cd;

	void ToText(char* dest, prof_stats* stats)
	{
		cd = CycleDiff();

		stats->avg_counter++;

		for(int i=0; i<9; i++)
		{	
			if(current_count[i] > stats->max_count[i][TICKS])
			{
				stats->max_count[i][TICKS]    = current_count[i];			
				stats->max_count[i][PERCENT]  = percent(stats->max_count[i][TICKS],total_tc); // x/100.0f
				stats->max_count[i][EFFSCEAS] = effsceas(stats->max_count[i][TICKS],cd);      // x/1000.0f
			}											

			stats->avg_count[i][TICKS]    += (current_count[i] - stats->avg_count[i][TICKS]) / stats->avg_counter;	
			stats->avg_count[i][PERCENT]  += (percent(stats->avg_count[i][TICKS],total_tc) - stats->avg_count[i][PERCENT]) / stats->avg_counter;
			stats->avg_count[i][EFFSCEAS] += (effsceas(stats->avg_count[i][TICKS],cd) - stats->avg_count[i][EFFSCEAS]) / stats->avg_counter;
		}
	
		dest+=sprintf(dest,("\nGFX  cur %.3f, %5.1f%% | avg %.3f, %5.1f%% | max %.3f, %5.1f%%\n"),
			effsceas(current_count[GFXC],cd)/1000.0f, percent(current_count[GFXC],total_tc)/100.0f,		
			stats->avg_count[GFXC][EFFSCEAS]/1000.0f, stats->avg_count[GFXC][PERCENT]/100.0f,		
			stats->max_count[GFXC][EFFSCEAS]/1000.0f, stats->max_count[GFXC][PERCENT]/100.0f);
		dest+=sprintf(dest,("AICA cur %.3f, %5.1f%% | avg %.3f, %5.1f%% | max %.3f, %5.1f%%\n"),
			effsceas(current_count[AICAC],cd)/1000.0f, percent(current_count[AICAC],total_tc)/100.0f,		
			stats->avg_count[AICAC][EFFSCEAS]/1000.0f, stats->avg_count[AICAC][PERCENT]/100.0f,		
			stats->max_count[AICAC][EFFSCEAS]/1000.0f, stats->max_count[AICAC][PERCENT]/100.0f);
		dest+=sprintf(dest,("ARM  cur %.3f, %5.1f%% | avg %.3f, %5.1f%% | max %.3f, %5.1f%%\n"),
			effsceas(current_count[ARMC],cd)/1000.0f, percent(current_count[ARMC],total_tc)/100.0f,		
			stats->avg_count[ARMC][EFFSCEAS]/1000.0f, stats->avg_count[ARMC][PERCENT]/100.0f,		
			stats->max_count[ARMC][EFFSCEAS]/1000.0f, stats->max_count[ARMC][PERCENT]/100.0f);
		dest+=sprintf(dest,("GDR  cur %.3f, %5.1f%% | avg %.3f, %5.1f%% | max %.3f, %5.1f%%\n"),
			effsceas(current_count[GDROMC],cd)/1000.0f, percent(current_count[GDROMC],total_tc)/100.0f,		
			stats->avg_count[GDROMC][EFFSCEAS]/1000.0f, stats->avg_count[GDROMC][PERCENT]/100.0f,		
			stats->max_count[GDROMC][EFFSCEAS]/1000.0f, stats->max_count[GDROMC][PERCENT]/100.0f);		
		dest+=sprintf(dest,("MAIN cur %.3f, %5.1f%% | avg %.3f, %5.1f%% | max %.3f, %5.1f%%\n"),
			effsceas(current_count[MAINC],cd)/1000.0f, percent(current_count[MAINC],total_tc)/100.0f,		
			stats->avg_count[MAINC][EFFSCEAS]/1000.0f, stats->avg_count[MAINC][PERCENT]/100.0f,		
			stats->max_count[MAINC][EFFSCEAS]/1000.0f, stats->max_count[MAINC][PERCENT]/100.0f);
		dest+=sprintf(dest,("LOOP cur %.3f, %5.1f%% | avg %.3f, %5.1f%% | max %.3f, %5.1f%%\n"),
			effsceas(current_count[DYNA_LOOPC],cd)/1000.0f, percent(current_count[DYNA_LOOPC],total_tc)/100.0f,		
			stats->avg_count[DYNA_LOOPC][EFFSCEAS]/1000.0f, stats->avg_count[DYNA_LOOPC][PERCENT]/100.0f,		
			stats->max_count[DYNA_LOOPC][EFFSCEAS]/1000.0f, stats->max_count[DYNA_LOOPC][PERCENT]/100.0f);
		dest+=sprintf(dest,("DYNA cur %.3f, %5.1f%% | avg %.3f, %5.1f%% | max %.3f, %5.1f%%\n"),
			effsceas(current_count[DYNAC],cd)/1000.0f, percent(current_count[DYNAC],total_tc)/100.0f,		
			stats->avg_count[DYNAC][EFFSCEAS]/1000.0f, stats->avg_count[DYNAC][PERCENT]/100.0f,		
			stats->max_count[DYNAC][EFFSCEAS]/1000.0f, stats->max_count[DYNAC][PERCENT]/100.0f);
		dest+=sprintf(dest,("REST cur %.3f, %5.1f%% | avg %.3f, %5.1f%% | max %.3f, %5.1f%%"),
			effsceas(current_count[RESTC],cd)/1000.0f, percent(current_count[RESTC],total_tc)/100.0f,		
			stats->avg_count[RESTC][EFFSCEAS]/1000.0f, stats->avg_count[RESTC][PERCENT]/100.0f,		
			stats->max_count[RESTC][EFFSCEAS]/1000.0f, stats->max_count[RESTC][PERCENT]/100.0f);
		
	}

};

extern prof_info profile_info;