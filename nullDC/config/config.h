#pragma once

#include "types.h"

/*
**	cfg* prototypes, if you pass NULL to a cfgSave* it will wipe out the section
**	} if you pass it to lpKey it will wipe out that particular entry
**	} if you add write to something it will create it if its not present
**	} ** Strings passed to LoadStr should be MAX_PATH in size ! **
*/

extern char cfgPath[512];
bool cfgOpen();
s32  EXPORT_CALL cfgLoadInt(const char * lpSection, const char * lpKey,s32 Default);
void EXPORT_CALL cfgSaveInt(const char * lpSection, const char * lpKey, s32 Int);
void EXPORT_CALL cfgLoadStr(const char * lpSection, const char * lpKey, char * lpReturn,const char* lpDefault);
void EXPORT_CALL cfgSaveStr(const char * lpSection, const char * lpKey, const char * lpString);
s32 EXPORT_CALL cfgExists(const char * Section, const char * Key);
void cfgSetVitual(const char * lpSection, const char * lpKey, const char * lpString);

