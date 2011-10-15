#ifndef FAKE_PLUGINSYSTEM_H
#define	FAKE_PLUGINSYSTEM_H

#include "nullDC/plugins/plugin_header.h"
#include "nullDC/plugins/gui_plugin_header.h"

#include "stdclass.h"

int GetLastError();

DLLHANDLE LoadLibrary(char * dllname);
int FreeLibrary(DLLHANDLE lib);
void * GetProcAddress(DLLHANDLE lib,char * procname);

#endif	/* FAKE_PLUGINSYSTEM_H */

