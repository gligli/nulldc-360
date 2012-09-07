#include <stdio.h>
#include <stdlib.h>
#include <xetypes.h>
#include <ppc/atomic.h>
#include <string.h>
#include <debug.h>
#include <errno.h>	
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <reent.h>
#include <sys/iosupport.h>
#include <threads/threads.h>
#include "md4.h"
#include "auth.h"
#include "smb.h"

#define ECHO_STRING "XENON ECHO"
#define ECHO_DELAY	10 // in seconds
#define O_DIROPEN 0x0008

typedef struct {
	void *f;
	int smb_fid;
	s64 filesize;
	s64 position;
	u32 mode;
	char name[256];
} FHANDLE;

static unsigned int keepalive_locked = 0;
static unsigned int smb_lock = 0;
static int UID = -1;
static int TID = -1;

static smbLogOn_in_t glogon_info;
static smbOpenShare_in_t gopenshare_info;
static ShareEntry_t ShareList __attribute__((aligned(64))); // Keep this aligned for DMA!
static ShareEntry_t sharelist[128] __attribute__((aligned(64))); // Keep this aligned for DMA!
static u8 SearchBuf[4096] __attribute__((aligned(64)));
static char smb_curdir[4096] __attribute__((aligned(64)));
static char smb_curpath[4096] __attribute__((aligned(64)));
static char smb_secpath[4096] __attribute__((aligned(64)));
//-------------------------------------------------------------------------

static void keepalive_lock(void) {
	lock(&keepalive_locked);
}

//-------------------------------------------------------------------------

static void keepalive_unlock(void) {
	unlock(&keepalive_locked);
}

//-------------------------------------------------------------------------

static void smb_io_lock(void) {
	lock(&smb_lock);
}

//-------------------------------------------------------------------------

static void smb_io_unlock(void) {
	unlock(&smb_lock);
}

//-------------------------------------------------------------------------

static void smb_GetPasswordHashes(smbGetPasswordHashes_in_t *in, smbGetPasswordHashes_out_t *out) {
	LM_Password_Hash((const unsigned char *) in->password, (unsigned char *) out->LMhash);
	NTLM_Password_Hash((const unsigned char *) in->password, (unsigned char *) out->NTLMhash);
}

//-------------------------------------------------------------- 

static int smb_LogOn(smbLogOn_in_t *logon) {
	register int r;

	if (UID != -1) {
		smb_LogOffAndX(UID);
		UID = -1;
	}

	r = smb_Connect(logon->serverIP, logon->serverPort);
	if (r < 0) {
		smb_printf("Failed to connect\n");
		return r;
	}

	smb_printf("Connected to %s port %d\n", logon->serverIP, logon->serverPort);

	r = smb_NegociateProtocol();
	if (r < 0) {
		smb_printf("Unable to negocialte protocol\n");
		return r;
	}

	r = smb_SessionSetupAndX(logon->User, logon->Password, logon->PasswordType);
	if (r < 0) {
		smb_printf("Unable to create a session\n");
		return r;
	}

	UID = r;

	memcpy((void *) &glogon_info, (void *) logon, sizeof (smbLogOn_in_t));

	keepalive_unlock();

	return 0;
}

//-------------------------------------------------------------- 

static int smb_GetShareList(smbGetShareList_in_t *getsharelist) {
	register int i, r, sharecount, shareindex;
	char tree_str[256];
	server_specs_t *specs;

	specs = (server_specs_t *) getServerSpecs();

	if (TID != -1) {
		smb_TreeDisconnect(UID, TID);
		TID = -1;
	}

	// Tree Connect on IPC slot
	sprintf(tree_str, "\\\\%s\\IPC$", specs->ServerIP);
	r = smb_TreeConnectAndX(UID, tree_str, NULL, 0);
	if (r < 0) {
		smb_printf("Unable to get tree.\n");
		return r;
	}

	TID = r;

	if (UID == -1) {
		smb_printf("Wrong UID.\n");
		return -3;
	}

	// does a 1st enum to count shares (+IPC)
	r = smb_NetShareEnum(UID, TID, (ShareEntry_t *) & ShareList, 0, 0);
	if (r < 0) {
		smb_printf("Share enum error %d.\n", r);
		return r;
	}

	sharecount = r;
	shareindex = 0;

	// now we list the following shares if any 
	for (i = 0; i < sharecount; i++) {

		r = smb_NetShareEnum(UID, TID, (ShareEntry_t *) & ShareList, i, 1);
		if (r < 0) {
			smb_printf("Share enum error %d.\n", r);
			return r;
		}

		// if the entry is not IPC, we send it on EE, and increment shareindex
		if ((strcmp(ShareList.ShareName, "IPC$")) && (shareindex < getsharelist->maxent)) {
			memcpy((void *) (getsharelist->EE_addr + (shareindex * sizeof (ShareEntry_t))), (void *) &ShareList, sizeof (ShareList));
			shareindex++;
		}
	}

	// disconnect the tree
	r = smb_TreeDisconnect(UID, TID);
	if (r < 0) {
		smb_printf("Tree disconnect error %d.\n", r);
		return r;
	}

	TID = -1;

	// return the number of shares	
	return shareindex;
}

//-------------------------------------------------------------- 

static int smb_OpenShare(smbOpenShare_in_t *openshare) {
	register int r;
	char tree_str[256];
	server_specs_t *specs;

	specs = (server_specs_t *) getServerSpecs();

	if (TID != -1) {
		smb_TreeDisconnect(UID, TID);
		TID = -1;
	}

	sprintf(tree_str, "\\\\%s\\%s", specs->ServerIP, openshare->ShareName);
	r = smb_TreeConnectAndX(UID, tree_str, openshare->Password, openshare->PasswordType);
	if (r < 0)
		return r;

	TID = r;

	memcpy((void *) &gopenshare_info, (void *) openshare, sizeof (smbOpenShare_in_t));

	return 0;
}

//-------------------------------------------------------------- 

static int smb_QueryDiskInfo(smbQueryDiskInfo_out_t *querydiskinfo) {
	if ((UID == -1) || (TID == -1))
		return -3;

	return smb_QueryInformationDisk(UID, TID, querydiskinfo);
}

//-------------------------------------------------------------- 

static char *prepare_path(char *path, char *full_path, int max_path) {
	register int i;

	// Move the path pointer to the start of the actual path
	if (strchr(path, ':') != NULL) {
		path = strchr(path, ':') + 1;
	}

	char *p = (char *) path;
	char *p2 = (char *) &path[strlen(path)];

	while ((*p == '\\') || (*p == '/'))
		p++;

	while ((*p2 == '\\') || (*p2 == '/'))
		*p2-- = 0;

	for (i = 0; i < strlen(p); i++) {
		if (p[i] == '/')
			p[i] = '\\';
	}

	if (strlen(p) > 0) {
		strncpy(full_path, smb_curdir, max_path - 1 - strlen(p));
		strcat(full_path, "\\");
		strcat(full_path, p);
	} else {
		strncpy(full_path, smb_curdir, max_path - 1);
		strcat(full_path, "\\");
	}

	return (char *) full_path;
}

//-------------------------------------------------------------- 

static int smb_open_r(struct _reent *re, void *fileStruct, const char *filename, int mode, int flags) {
	FHANDLE *fh = (FHANDLE*) fileStruct;
	register int r = -1;
	s64 filesize;

	if (!filename) {
		re->_errno = ENOENT;
		return -1;
	}

	if ((UID == -1) || (TID == -1)) {
		re->_errno = EINVAL;
		return -1;
	}

	char *path = prepare_path((char *) filename, smb_curpath, 4096);

	smb_io_lock();

	if (fh) {
		r = smb_OpenAndX(UID, TID, path, &filesize, mode);
		if (r < 0) {
			if (r == -1) {
				re->_errno = EIO;
			} else if (r == -2) {
				re->_errno = EPERM;
			} else if (r == -3) {
				re->_errno = ENOENT;
			} else {
				re->_errno = EIO;
			}
			r = -1;
		} else {
			fh->smb_fid = r;
			fh->mode = mode;
			fh->filesize = filesize;
			fh->position = 0;
			if (fh->mode & O_TRUNC)
				fh->filesize = 0;
			else if (fh->mode & O_APPEND)
				fh->position = filesize;
			strncpy(fh->name, path, 256);
			r = 0;
		}
	} else {
		re->_errno = EMFILE;
		r = -1;
	}

	smb_io_unlock();

	return (r == 0) ? (int) fh : -1;
}

//-------------------------------------------------------------- 

static s64 smb_lseek64_r(struct _reent *re, int fd, s64 pos, int where) {
	s64 r;
	FHANDLE *fh = (FHANDLE*) fd;
	smb_io_lock();

	switch (where) {
		case SEEK_CUR:
			r = fh->position + pos;
			if (r > fh->filesize) {
				re->_errno = EINVAL;
				r = -1;
				goto io_unlock;
			}
			break;
		case SEEK_SET:
			r = pos;
			if (fh->filesize < pos) {
				re->_errno = EINVAL;
				r = -1;
				goto io_unlock;
			}
			break;
		case SEEK_END:
			r = fh->filesize;
			break;
		default:
			re->_errno = EINVAL;
			r = -1;
			goto io_unlock;
	}

	fh->position = r;

io_unlock:
	smb_io_unlock();

	return r;
}

//-------------------------------------------------------------- 

static off_t smb_seek_r(struct _reent *r, int fd, off_t pos, int dir) {
	return (off_t) smb_lseek64_r(r, fd, pos, dir);
}

//-------------------------------------------------------------- 

static ssize_t smb_read_r(struct _reent *re, int fd, char *buf, size_t size) {
	FHANDLE *fh = (FHANDLE*) fd;
	register int r, rpos;
	register u32 nbytes;

	if ((UID == -1) || (TID == -1) || (fh->smb_fid == -1)) {
		re->_errno = EINVAL;
		return -1;
	}

	if ((fh->position + size) > fh->filesize)
		size = fh->filesize - fh->position;

	smb_io_lock();

	rpos = 0;

	while (size) {
		nbytes = MAX_RD_BUF;
		if (size < nbytes)
			nbytes = size;

		r = smb_ReadAndX(UID, TID, fh->smb_fid, fh->position, (void *) (buf + rpos), (u16) nbytes);
		if (r < 0) {
			re->_errno = EIO;
			rpos = -1;
			goto io_unlock;
		}

		rpos += nbytes;
		size -= nbytes;
		fh->position += nbytes;
	}

io_unlock:
	smb_io_unlock();

	return rpos;
}

//-------------------------------------------------------------- 

static ssize_t smb_write_r(struct _reent *re, int fd, const char *buf, size_t size) {
	FHANDLE *fh = (FHANDLE*) fd;
	register int r, wpos;
	register u32 nbytes;

	if ((UID == -1) || (TID == -1) || (fh->smb_fid == -1)) {
		re->_errno = EINVAL;
		return -1;
	}

	if ((!(fh->mode & O_RDWR)) && (!(fh->mode & O_WRONLY))) {
		re->_errno = EPERM;
		return -1;
	}

	smb_io_lock();

	wpos = 0;

	while (size) {
		nbytes = MAX_WR_BUF;
		if (size < nbytes)
			nbytes = size;

		r = smb_WriteAndX(UID, TID, fh->smb_fid, fh->position, (void *) (buf + wpos), (u16) nbytes);
		if (r < 0) {
			wpos = -EIO;
			goto io_unlock;
		}

		wpos += nbytes;
		size -= nbytes;
		fh->position += nbytes;
		if (fh->position > fh->filesize)
			fh->filesize += fh->position - fh->filesize;
	}

io_unlock:
	smb_io_unlock();

	return wpos;
}

//-------------------------------------------------------------- 

static int smb_fstat_r(struct _reent *re, int fd, struct stat *st) {
	register int r = -1;
	FHANDLE *fh = (FHANDLE*) fd;

	if (fh == NULL) {
		re->_errno = EBADF;
	}

	if ((UID == -1) || (TID == -1) || (fh->smb_fid == -1)) {
		re->_errno = EINVAL;
		return -1;
	}
	smb_io_lock();

	st->st_mode = fh->mode;
	st->st_size = fh->filesize;

	smb_io_unlock();
	return r;
}

//-------------------------------------------------------------- 

static int smb_stat_r(struct _reent *re, const char *filename, struct stat *st) {
	register int r;
	PathInformation_t info;

	if (!filename) {
		re->_errno = ENOENT;
		return -1;
	}

	if ((UID == -1) || (TID == -1)) {
		re->_errno = EINVAL;
		return -1;
	}

	char *path = prepare_path((char *) filename, smb_curpath, 4096);

	smb_io_lock();

	memset((void *) st, 0, sizeof (struct stat));

	r = smb_QueryPathInformation(UID, TID, (PathInformation_t *) & info, path);
	if (r < 0) {
		re->_errno = EIO;
		r = -1;
		goto io_unlock;
	}

	// 64 bit :s
	st->st_ctime = info.Created;
	st->st_atime = info.LastAccess;
	st->st_mtime = info.Change;

	st->st_size = (int) (info.EndOfFile & 0xffffffff);
	//stat->st_size = (int) ((info.EndOfFile >> 32) & 0xffffffff);

	if (info.FileAttributes & EXT_ATTR_DIRECTORY)
		st->st_mode |= S_IFDIR;
	else
		st->st_mode |= S_IFREG;

	r = 0;
io_unlock:
	smb_io_unlock();
	return r;
}

//-------------------------------------------------------------- 

static int smb_close_r(struct _reent *re, int fd) {
	FHANDLE *fh = (FHANDLE*) fd;
	register int r = 0;

	if ((UID == -1) || (TID == -1) || (fh->smb_fid == -1)) {
		re->_errno = EINVAL;
		return -1;
	}

	smb_io_lock();

	if (fh) {

		if (fh->mode != O_DIROPEN) {
			r = smb_Close(UID, TID, fh->smb_fid);
			if (r != 0) {
				re->_errno = EIO;
				goto io_unlock;
			}
		}
		memset(fh, 0, sizeof (FHANDLE));
		fh->smb_fid = -1;
		r = 0;
	}

io_unlock:
	smb_io_unlock();

	return r;
}

//-------------------------------------------------------------- 

static DIR_ITER* smb_diropen_r(struct _reent *re, DIR_ITER *dirState, const char *dirname) {
	FHANDLE *ret = NULL;
	FHANDLE *fh = (FHANDLE*) (dirState->dirStruct);
	register int r = 0;
	PathInformation_t info;

	memset(fh, 0, sizeof (FHANDLE));

	if (!dirname) {
		re->_errno = ENOENT;
		return NULL;
	}

	if ((UID == -1) || (TID == -1)) {
		re->_errno = EINVAL;
		return NULL;
	}

	char *path = prepare_path((char *) dirname, smb_curpath, 4096);

	smb_io_lock();

	// test if the dir exists
	r = smb_QueryPathInformation(UID, TID, (PathInformation_t *) & info, path);
	if (r < 0) {
		ret = NULL;
		re->_errno = EIO;
		goto io_unlock;
	}

	if (!(info.FileAttributes & EXT_ATTR_DIRECTORY)) {
		ret = NULL;
		re->_errno = ENOTDIR;
		goto io_unlock;
	}

	if (fh) {
		fh->mode = O_DIROPEN;
		fh->filesize = 0;
		fh->position = 0;
		fh->smb_fid = -1;

		strncpy(fh->name, path, 255);
		if (fh->name[strlen(fh->name) - 1] != '\\')
			strcat(fh->name, "\\");
		strcat(fh->name, "*");

		ret = fh;
	} else {
		ret = NULL;
		re->_errno = EMFILE;
	}

io_unlock:
	smb_io_unlock();

	return (DIR_ITER*) ret;
}

//-------------------------------------------------------------- 

static int smb_dirclose_r(struct _reent *r, DIR_ITER *dirState) {
	FHANDLE *fh = (FHANDLE*) (dirState->dirStruct);
	return smb_close_r(r, (int) fh);
}

//-------------------------------------------------------------- 

static int smb_dirnext_r(struct _reent *re, DIR_ITER *dirState, char *filename, struct stat *filestat) {
	FHANDLE *fh = (FHANDLE*) (dirState->dirStruct);
	register int r = -1;

	if ((UID == -1) || (TID == -1)) {
		re->_errno = EINVAL;
		return -1;
	}

	smb_io_lock();

	memset((void *) filestat, 0, sizeof (struct stat));

	SearchInfo_t *info = (SearchInfo_t *) SearchBuf;

	if (fh->smb_fid == -1) {
		r = smb_FindFirstNext2(UID, TID, fh->name, TRANS2_FIND_FIRST2, info);
		if (r < 0) {
			r = -1;
			re->_errno = EIO;
			goto io_unlock;
		}
		fh->smb_fid = info->SID;
		r = 1;
	} else {
		info->SID = fh->smb_fid;
		r = smb_FindFirstNext2(UID, TID, NULL, TRANS2_FIND_NEXT2, info);
		if (r < 0) {
			r = -1;
			re->_errno = EIO;
			goto io_unlock;
		}
		r = 1;
	}

	if (r == 1) {
		filestat->st_ctime = info->fileInfo.Created;
		filestat->st_atime = info->fileInfo.LastAccess;
		filestat->st_mtime = info->fileInfo.Change;
		filestat->st_size = info->fileInfo.EndOfFile;

		if (info->fileInfo.FileAttributes & EXT_ATTR_DIRECTORY)
			filestat->st_mode |= S_IFDIR;
		else
			filestat->st_mode |= S_IFREG;

		strncpy(filename, info->FileName, 256);

		// Success
		r = 0;
	}


io_unlock:
	smb_io_unlock();

	return r;
}

//-------------------------------------------------------------------------

static void keepalive_thread() {
	register int r, opened_share = 0;

	while (1) {
		smb_io_lock();

		// echo the SMB server
		r = smb_Echo(ECHO_STRING, strlen(ECHO_STRING));
		if (r < 0) {
			keepalive_lock();

			if (TID != -1)
				opened_share = 1;

			if (UID != -1) {
				r = smb_LogOn((smbLogOn_in_t *) & glogon_info);
				if (r == 0) {
					if (opened_share)
						smb_OpenShare((smbOpenShare_in_t *) & gopenshare_info);
				}
			}

			keepalive_unlock();
		}

		smb_io_unlock();
		
		// every 10 sec
		delay(ECHO_DELAY);
	}
}

//-------------------------------------------------------------------------

static const devoptab_t smb_optab = {
	"devoptab",
	sizeof (FHANDLE),
	smb_open_r,
	smb_close_r,
	smb_write_r,
	smb_read_r,
	smb_seek_r,
	smb_fstat_r,
	smb_stat_r,
	NULL,
	NULL, //smb_unlink_r,
	NULL,
	NULL, //smb_rename_r,
	NULL, //smb_mkdir_r,
	sizeof (FHANDLE),
	smb_diropen_r,
	NULL,
	smb_dirnext_r,
	smb_dirclose_r,
	NULL, //smb_statvfs_r,
	NULL,
	NULL,
	NULL, /* Device data */
	NULL,
	NULL
};

//-------------------------------------------------------------------------

int smbInit(const char * mount_name, const char * ip, int port, const char * share, const char * user, const char * password) {
	static int initialised = 0;
	smbLogOn_in_t logon;
	smbOpenShare_in_t openshare;
	smbGetPasswordHashes_in_t passwd;
	smbGetPasswordHashes_out_t passwdhashes;

	if (initialised == 1) {
		return 1;
	}

	// some check
	if (mount_name == NULL || ip == NULL || port == 0 || share == NULL) {
		printf("* Check smb settings\n");
		return -1;
	}

	strcpy(logon.serverIP, ip);
	logon.serverPort = port;
	strcpy(logon.User, user);

	// create password hash
	if (password) {
		strcpy(passwd.password, password);
		smb_GetPasswordHashes(&passwd, &passwdhashes);

		// login
		memcpy((void *) logon.Password, (void *) &passwdhashes, sizeof (passwdhashes));
		logon.PasswordType = HASHED_PASSWORD;

		// share
		memcpy((void *) openshare.Password, (void *) &passwdhashes, sizeof (passwdhashes));
		openshare.PasswordType = HASHED_PASSWORD;
	} else {
		logon.PasswordType = NO_PASSWORD;
		openshare.PasswordType = NO_PASSWORD;
	}
	// try to log in
	if (smb_LogOn(&logon) != 0) {
		printf("* Failed to login\n");
		return -2;
	}

	// open a share	
	memcpy((void *) openshare.Password, (void *) &passwdhashes, sizeof (passwdhashes));
	openshare.PasswordType = HASHED_PASSWORD;
	strcpy(openshare.ShareName, share);

	if (smb_OpenShare(&openshare) != 0) {
		printf("* Failed to access share\n");
		return -3;
	}

	// keep alive thread
	PTHREAD th = thread_create((void*) keepalive_thread, 0, 0, THREAD_FLAG_CREATE_SUSPENDED);
	thread_set_processor(th, 1);
	thread_resume(th);

	// create device
	strcpy(smb_optab.name, mount_name);
	AddDevice(&smb_optab);

	printf("* Mounted \\\\%s\\%s\\ as %s \n", ip, share, mount_name);
	
	initialised = 1;
	return 1;
}

//-------------------------------------------------------------------------
#include <dirent.h>
#include <stdio.h>

void smbTest() {
	struct dirent *pDirent;
	DIR *pDir;
	pDir = opendir("smb:/snes9xgx/");
	if (pDir == NULL) {
		printf("Cannot open directory smb:/\n");
		return;
	}

	while ((pDirent = readdir(pDir)) != NULL) {
		printf("[%s]\n", pDirent->d_name);
	}
	closedir(pDir);
}

void r_smbTest() {
	int i = 0;
	int ret = 0;

	smbGetPasswordHashes_in_t passwd;
	smbGetPasswordHashes_out_t passwdhashes;

	strcpy(passwd.password, "cc");
	// password
	smb_GetPasswordHashes(&passwd, &passwdhashes);

	printf("OK\n");
	printf("LMhash   = 0x");
	for (i = 0; i < 16; i++)
		printf("%02X", passwdhashes.LMhash[i]);
	printf("\n");
	printf("NTLMhash = 0x");
	for (i = 0; i < 16; i++)
		printf("%02X", passwdhashes.NTLMhash[i]);
	printf("\n");

	// logon
	smbLogOn_in_t logon;
	strcpy(logon.serverIP, "192.168.1.98");
	logon.serverPort = 445;
	strcpy(logon.User, "cc");
	memcpy((void *) logon.Password, (void *) &passwdhashes, sizeof (passwdhashes));
	logon.PasswordType = HASHED_PASSWORD;

	printf("Logon\n");
	ret = smb_LogOn(&logon);
	if (ret == 0) {
		printf("Logged in\n");
	} else {
		printf("Error %d ", ret);
		return;
	}

	// list shares
	smbGetShareList_in_t getsharelist;
	getsharelist.EE_addr = (void *) &sharelist[0];
	getsharelist.maxent = 128;

	ret = smb_GetShareList(&getsharelist);

	printf("Get share list\n");
	if (ret >= 0) {
		printf("OK count = %d\n", ret);
		for (i = 0; i < ret; i++) {
			printf("\t\t - %s: %s\n", sharelist[i].ShareName, sharelist[i].ShareComment);
		}
	} else {
		printf("Error %d\n", ret);
		return;
	}

	// open a share
	smbOpenShare_in_t openshare;
	memcpy((void *) openshare.Password, (void *) &passwdhashes, sizeof (passwdhashes));
	openshare.PasswordType = HASHED_PASSWORD;
	strcpy(openshare.ShareName, "web");

	printf("Open share\n");
	ret = smb_OpenShare(&openshare);

	if (ret == 0) {
		printf("OK\n");
	} else {
		printf("Error %d\n", ret);
		return;
	}

	// query space info
	smbQueryDiskInfo_out_t querydiskinfo;

	printf("QUERYDISKINFO... ");
	ret = smb_QueryDiskInfo(&querydiskinfo);
	if (ret == 0) {
		printf("OK\n");
		printf("Total Units = %d, BlocksPerUnit = %d\n", querydiskinfo.TotalUnits, querydiskinfo.BlocksPerUnit);
		printf("BlockSize = %d, FreeUnits = %d\n", querydiskinfo.BlockSize, querydiskinfo.FreeUnits);
	} else {
		printf("Error %d\n", ret);
		return;
	}

	// open file :)
	FHANDLE Handle = {0};
	int * fd = (int*) &Handle;
	struct _reent reent;
	ret = smb_open_r(&reent, fd, "smb:/index.html", O_RDONLY, 0);
	if (ret != -1) {
		printf("Found file !\n");
		printf("Size:%lld, File mode:%d\n", Handle.filesize, Handle.mode);
	} else {
		printf("File not found %d\n", reent._errno);
		return;
	}

	// read file :p
	void * buf = (void*) malloc(Handle.filesize);
	smb_read_r(&reent, fd, buf, Handle.filesize);
	printf("file:\n%s\n", buf);
	smb_close_r(&reent, &Handle);

	// list dir
	DIR_ITER dir;
	dir.dirStruct = (void*) &Handle;
	DIR_ITER * rep = smb_diropen_r(&reent, &dir, "smb:/");
	char * filename = (char*) malloc(512);
	if (rep != NULL) {
		struct stat filestat;
		while (smb_dirnext_r(&reent, &dir, filename, &filestat) == 0) {
			printf("%s\n", filename);
		}

		smb_dirclose_r(&reent, &dir);
	} else {
		printf("Diropen failed %d\n", reent._errno);
	}
}


