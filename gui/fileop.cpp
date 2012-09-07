/****************************************************************************
 * Snes9x Nintendo Wii/Gamecube Port
 *
 * softdev July 2006
 * crunchy2 May 2007
 * Michniewski 2008
 * Tantric 2008-2010
 *
 * fileop.cpp
 *
 * File operations
 ***************************************************************************/

#include <xetypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <zlib.h>
#include <malloc.h>
#include <diskio/ata.h>
#include <ppc/atomic.h>
#include <xenon_soc/xenon_power.h>
#include <debug.h>

#include <libxtaf/xtaf.h>
#include "snes9xgx.h"
#include "fileop.h"
#include "menu.h"
#include "filebrowser.h"
#include "gui/gui.h"

#define THREAD_SLEEP 100

unsigned char *savebuffer = NULL;
//static mutex_t bufferLock = LWP_MUTEX_NULL;
FILE * file; // file pointer - the only one we should ever use!
bool unmountRequired[7] = {false, false, false, false, false, false, false};
bool isMounted[7] = {false, false, false, false, false, false, false};

//#ifdef HW_RVL
//	const DISC_INTERFACE* sd = &__io_wiisd;
//	const DISC_INTERFACE* usb = &__io_usbstorage;
//	const DISC_INTERFACE* dvd = &__io_wiidvd;
//#else
//	const DISC_INTERFACE* carda = &__io_gcsda;
//	const DISC_INTERFACE* cardb = &__io_gcsdb;
//	const DISC_INTERFACE* dvd = &__io_gcdvd;
//#endif

// folder parsing thread
//static lwp_t parsethread = LWP_THREAD_NULL;
static DIR *dir = NULL;
static bool parseHalt = true;
static bool parseFilter = true;
static bool ParseDirEntries();
int selectLoadedFile = 0;

// device thread
//static lwp_t devicethread = LWP_THREAD_NULL;
static bool deviceHalt = true;

static unsigned char xenon_thread_stack[6 * 0x10000];
static unsigned int __attribute__((aligned(128))) _file_lock = 0;
static int _parse_thread_suspended = 0;

/****************************************************************************
 * ResumeDeviceThread
 *
 * Signals the device thread to start, and resumes the thread.
 ***************************************************************************/
void
ResumeDeviceThread()
{
	deviceHalt = false;
	//LWP_ResumeThread(devicethread);
}

/****************************************************************************
 * HaltGui
 *
 * Signals the device thread to stop.
 ***************************************************************************/
void
HaltDeviceThread()
{
	deviceHalt = true;
}

/****************************************************************************
 * HaltParseThread
 *
 * Signals the parse thread to stop.
 ***************************************************************************/
void
HaltParseThread()
{
	lock(&_file_lock);
	parseHalt = true;
	_parse_thread_suspended = 1;
	unlock(&_file_lock);
}

static void *
parsecallback(void *arg)
{
	int parse = 0;
	while (exitThreads == 0) {

		lock(&_file_lock);
		if (_parse_thread_suspended == 0) {
			while (ParseDirEntries())
				usleep(THREAD_SLEEP);
		}
		_parse_thread_suspended = 1;
		unlock(&_file_lock);


		//while(ParseDirEntries())
		//	usleep(THREAD_SLEEP);
		//LWP_SuspendThread(parsethread);
	}
	return NULL;
}

/****************************************************************************
 * InitDeviceThread
 *
 * libOGC provides a nice wrapper for LWP access.
 * This function sets up a new local queue and attaches the thread to it.
 ***************************************************************************/
void
InitDeviceThread()
{
	//xenon_run_thread_task(3, xenon_thread_stack + (3 * 0x10000) - 0x100, parsecallback);
}

/****************************************************************************
 * UnmountAllFAT
 * Unmounts all FAT devices
 ***************************************************************************/
void UnmountAllFAT()
{

}

/****************************************************************************
 * MountFAT
 * Checks if the device needs to be (re)mounted
 * If so, unmounts the device
 * Attempts to mount the device specified
 * Sets libfat to use the device by default
 ***************************************************************************/
void MountAllFAT()
{
	fatInitDefault();
	//XTAFMount();
}

/****************************************************************************
 * MountDVD()
 *
 * Tests if a ISO9660 DVD is inserted and available, and mounts it
 ***************************************************************************/
bool MountDVD(bool silent)
{
	return false;
}

bool FindDevice(char * filepath, int * device)
{
	if (strncmp(filepath, "uda", 3) == 0) {
		*device = DEVICE_USB;
		return true;
	} else if (strncmp(filepath, "sda", 3) == 0) {
		*device = DEVICE_HDD;
		return true;
	} else if (strncmp(filepath, "smb", 3) == 0) {
		*device = DEVICE_SMB;
		return true;
	} else if (strncmp(filepath, "dvd", 3) == 0) {
		*device = DEVICE_DVD;
		return true;
	}
	return false;
}

char * StripDevice(char * path)
{
	if (path == NULL)
		return NULL;

	char * newpath = strchr(path, '/');

	if (newpath != NULL)
		newpath++;

	return newpath;
}

/****************************************************************************
 * ChangeInterface
 * Attempts to mount/configure the device specified
 ***************************************************************************/
bool ChangeInterface(int device, bool silent)
{
	//        if (isMounted[device])
	//                return true;

	//bool mounted = false;
	bool mounted = true;

	switch (device) {
	case DEVICE_HDD:
	case DEVICE_USB:
		//mounted = MountFAT(device, silent);
		return true;
		break;
	case DEVICE_DVD:
		return false;
		//mounted = MountDVD(silent);
		break;
	case DEVICE_SMB:
		//mounted = ConnectShare(silent);
		break;
	}

	return mounted;
}

bool ChangeInterface(char * filepath, bool silent)
{
	int device = -1;

	if (!FindDevice(filepath, &device))
		return false;

	return ChangeInterface(device, silent);
}

void CreateAppPath(char * origpath)
{
	if (!origpath || origpath[0] == 0)
		return;

	char * path = strdup(origpath); // make a copy so we don't mess up original

	if (!path)
		return;

	char * loc = strrchr(path, '/');
	if (loc != NULL)
		*loc = 0; // strip file name

	int pos = 0;

	if (ChangeInterface(&path[pos], SILENT))
		snprintf(appPath, MAXPATHLEN - 1, "%s", &path[pos]);

	free(path);
}

static char *GetExt(char *file)
{
	if (!file)
		return NULL;

	char *ext = strrchr(file, '.');
	if (ext != NULL) {
		ext++;
		int extlen = strlen(ext);
		if (extlen > 5)
			return NULL;
	}
	return ext;
}

bool GetFileSize(int i)
{
	if (browserList[i].length > 0)
		return true;

	struct stat filestat;
	char path[MAXPATHLEN + 1];
	snprintf(path, MAXPATHLEN, "%s%s", browser.dir, browserList[i].filename);

	if (stat(path, &filestat) < 0)
		return false;

	browserList[i].length = filestat.st_size;
	return true;
}

static bool ParseDirEntries()
{
	if (!dir)
		return false;

	char *ext;
	struct dirent *entry = NULL;
	int isdir;

	int i = 0;

	while (i < 20 && !parseHalt) {
		entry = readdir(dir);

		if (entry == NULL)
			break;

		if (entry->d_name[0] == '.' && entry->d_name[1] != '.')
			continue;

		if (strcmp(entry->d_name, "..") == 0) {
			isdir = 1;
		} else {
			if (entry->d_type == DT_DIR)
				isdir = 1;
			else
				isdir = 0;

			// don't show the file if it's not a valid ROM
			if (parseFilter && !isdir) {
				ext = GetExt(entry->d_name);

				if (ext == NULL)
					continue;

				if (stricmp(ext, "gdi") != 0 && stricmp(ext, "mds") != 0)
					continue;
			}
		}

		if (!AddBrowserEntry()) {
			parseHalt = true;
			break;
		}

		snprintf(browserList[browser.numEntries + i].filename, MAXJOLIET, "%s", entry->d_name);
		browserList[browser.numEntries + i].isdir = isdir; // flag this as a dir

		if (isdir) {
			if (strcmp(entry->d_name, "..") == 0)
				sprintf(browserList[browser.numEntries + i].displayname, "Up One Level");
			else
				snprintf(browserList[browser.numEntries + i].displayname, MAXJOLIET, "%s", browserList[browser.numEntries + i].filename);
			browserList[browser.numEntries + i].icon = ICON_FOLDER;
		} else {
			StripExt(browserList[browser.numEntries + i].displayname, browserList[browser.numEntries + i].filename); // hide file extension
		}
		i++;
	}

	if (!parseHalt) {
		// Sort the file list
		if (i >= 0)
			qsort(browserList, browser.numEntries + i, sizeof (BROWSERENTRY), FileSortCallback);

		browser.numEntries += i;
	}

	if (entry == NULL || parseHalt) {
		closedir(dir); // close directory
		dir = NULL;

		// try to find and select the last loaded file
		if (selectLoadedFile == 1 && !parseHalt && loadedFile[0] != 0 && browser.dir[0] != 0) {
			int indexFound = -1;

			for (int j = 1; j < browser.numEntries; j++) {
				if (strcmp(browserList[j].filename, loadedFile) == 0) {
					indexFound = j;
					break;
				}
			}

			// move to this file
			if (indexFound > 0) {
				if (indexFound >= FILE_PAGESIZE) {
					int newIndex = (floor(indexFound / (float) FILE_PAGESIZE)) * FILE_PAGESIZE;

					if (newIndex + FILE_PAGESIZE > browser.numEntries)
						newIndex = browser.numEntries - FILE_PAGESIZE;

					if (newIndex < 0)
						newIndex = 0;

					browser.pageIndex = newIndex;
				}
				browser.selIndex = indexFound;
			}
			selectLoadedFile = 2; // selecting done
		}
		return false; // no more entries
	}
	return true; // more entries
}

/***************************************************************************
 * Browse subdirectories
 **************************************************************************/
int
ParseDirectory(bool waitParse, bool filter)
{
	int retry = 1;
	bool mounted = false;
	parseFilter = filter;

	ResetBrowser(); // reset browser

	// add trailing slash
	if (browser.dir[strlen(browser.dir) - 1] != '/')
		strcat(browser.dir, "/");

	// open the directory
	while (dir == NULL && retry == 1) {
		mounted = ChangeInterface(browser.dir, NOTSILENT);

		if (mounted)
			dir = opendir(browser.dir);
		else
			return -1;

		if (dir == NULL) {
			retry = ErrorPromptRetry("Error opening directory!");
		}
	}

	// if we can't open the dir, try higher levels
	if (dir == NULL) {
		char * devEnd = strrchr(browser.dir, '/');

		while (!IsDeviceRoot(browser.dir)) {
			devEnd[0] = 0; // strip slash
			devEnd = strrchr(browser.dir, '/');

			if (devEnd == NULL)
				break;

			devEnd[1] = 0; // strip remaining file listing
			dir = opendir(browser.dir);
			if (dir)
				break;
		}
	}

	if (dir == NULL)
		return -1;

	if (IsDeviceRoot(browser.dir)) {
		AddBrowserEntry();
		sprintf(browserList[0].filename, "..");
		sprintf(browserList[0].displayname, "Up One Level");
		browserList[0].length = 0;
		browserList[0].isdir = 1; // flag this as a dir
		browserList[0].icon = ICON_FOLDER;
		browser.numEntries++;
	}

	parseHalt = false;
	//ParseDirEntries(); // index first 20 entries

	while (ParseDirEntries());

#if 0		
	//LWP_ResumeThread(parsethread); // index remaining entries

	lock(&_file_lock);
	_parse_thread_suspended = 0;
	unlock(&_file_lock);

	int _end = 1;

	if (waitParse) // wait for complete parsing
	{
		ShowAction("Loading...");

		//		while(!LWP_ThreadIsSuspended(parsethread))
		//			usleep(THREAD_SLEEP);

		while (_end == 1) {
			lock(&_file_lock);
			if (_parse_thread_suspended == 1) {
				_end = 0;
			}
			unlock(&_file_lock);
			usleep(THREAD_SLEEP);
		}
		CancelAction();
	}
#endif
	return browser.numEntries;
}

/****************************************************************************
 * AllocSaveBuffer ()
 * Clear and allocate the savebuffer
 ***************************************************************************/
void
AllocSaveBuffer()
{
	memset(savebuffer, 0, SAVEBUFFERSIZE);
}

/****************************************************************************
 * FreeSaveBuffer ()
 * Free the savebuffer memory
 ***************************************************************************/
void
FreeSaveBuffer()
{

}

/****************************************************************************
 * LoadFile
 ***************************************************************************/
size_t
LoadFile(char * rbuffer, char *filepath, size_t length, bool silent)
{
	char zipbuffer[2048];
	size_t size = 0, offset = 0, readsize = 0;
	int retry = 1;
	int device;

	if (!FindDevice(filepath, &device))
		return 0;

	// stop checking if devices were removed/inserted
	// since we're loading a file
	HaltDeviceThread();

	// halt parsing
	HaltParseThread();

	// open the file
	while (retry) {
		if (!ChangeInterface(device, silent))
			break;

		file = fopen(filepath, "rb");

		if (!file) {
			if (silent)
				break;

			retry = ErrorPromptRetry("Error opening file!");
			continue;
		}

		if (length > 0 && length <= 2048) // do a partial read (eg: to check file header)
		{
			size = fread(rbuffer, 1, length, file);
		} else // load whole file
		{
			readsize = fread(zipbuffer, 1, 32, file);

			if (!readsize) {
				unmountRequired[device] = true;
				retry = ErrorPromptRetry("Error reading file!");
				fclose(file);
				continue;
			}

			fseeko(file, 0, SEEK_END);
			size = ftello(file);
			fseeko(file, 0, SEEK_SET);

			while (!feof(file)) {
				//ShowProgress("Loading...", offset, size);
				readsize = fread(rbuffer + offset, 1, 4096, file); // read in next chunk

				if (readsize <= 0)
					break; // reading finished (or failed)

				offset += readsize;
			}
			size = offset;
			CancelAction();

		}
		retry = 0;
		fclose(file);
	}

	// go back to checking if devices were inserted/removed
	ResumeDeviceThread();
	CancelAction();
	return size;
}

size_t LoadFile(char * filepath, bool silent)
{
	return LoadFile((char *) savebuffer, filepath, 0, silent);
}

/****************************************************************************
 * SaveFile
 * Write buffer to file
 ***************************************************************************/
size_t
SaveFile(char * buffer, char *filepath, size_t datasize, bool silent)
{

	printf("SaveFile :%s\n", filepath);
	size_t written = 0;
	size_t writesize, nextwrite;
	int retry = 1;
	int device;

	if (!FindDevice(filepath, &device))
		return 0;

	if (datasize == 0)
		return 0;

	// stop checking if devices were removed/inserted
	// since we're saving a file
	HaltDeviceThread();

	// halt parsing
	HaltParseThread();

	ShowAction("Saving...");

	while (!written && retry == 1) {
		if (!ChangeInterface(device, silent))
			break;

		file = fopen(filepath, "wb");

		if (!file) {
			if (silent)
				break;

			retry = ErrorPromptRetry("Error creating file!");
			continue;
		}

		while (written < datasize) {
			if (datasize - written > 4096) nextwrite = 4096;
			else nextwrite = datasize - written;
			writesize = fwrite(buffer + written, 1, nextwrite, file);
			if (writesize != nextwrite) break; // write failure
			written += writesize;
		}
		fclose(file);

		if (written != datasize) written = 0;

		if (!written) {
			unmountRequired[device] = true;
			if (silent) break;
			retry = ErrorPromptRetry("Error saving file!");
		}
	}

	// go back to checking if devices were inserted/removed
	ResumeDeviceThread();
	CancelAction();
	return written;
}

size_t SaveFile(char * filepath, size_t datasize, bool silent)
{
	return SaveFile((char *) savebuffer, filepath, datasize, silent);
}
