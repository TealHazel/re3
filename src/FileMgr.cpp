#define _CRT_SECURE_NO_WARNINGS
#include <fcntl.h>
#include <direct.h>
#include "common.h"
#include "patcher.h"
#include "FileMgr.h"

const char *_psGetUserFilesFolder();

/*
 * Windows FILE is BROKEN for GTA.
 *
 * We need to support mapping between LF and CRLF for text files
 * but we do NOT want to end the file at the first sight of a SUB character.
 * So here is a simple implementation of a FILE interface that works like GTA expects.
 */

struct myFILE
{
	bool isText;
	FILE *file;
};

#define NUMFILES 20
static myFILE myfiles[NUMFILES];

/* Force file to open as binary but remember if it was text mode */
static int
myfopen(const char *filename, const char *mode)
{
	int fd;
	char realmode[10], *p;

	for(fd = 1; fd < NUMFILES; fd++)
		if(myfiles[fd].file == nil)
			goto found;
	return 0;	// no free fd
found:
	myfiles[fd].isText = strchr(mode, 'b') == nil;
	p = realmode;
	while(*mode)
		if(*mode != 't' && *mode != 'b')
			*p++ = *mode++;
		else
			mode++;
	*p++ = 'b';
	*p = '\0';
	myfiles[fd].file = fopen(filename, realmode);
	if(myfiles[fd].file == nil)
		return 0;
	return fd;
}

static int
myfclose(int fd)
{
	int ret;
	assert(fd < NUMFILES);
	if(myfiles[fd].file){
		ret = fclose(myfiles[fd].file);
		myfiles[fd].file = nil;
		return ret;
	}
	return EOF;
}

static int
myfgetc(int fd)
{
	int c;
	c = fgetc(myfiles[fd].file);
	if(myfiles[fd].isText && c == 015){
		/* translate CRLF to LF */
		c = fgetc(myfiles[fd].file);
		if(c == 012)
			return c;
		ungetc(c, myfiles[fd].file);
		return 015;
	}
	return c;
}

static int
myfputc(int c, int fd)
{
	/* translate LF to CRLF */
	if(myfiles[fd].isText && c == 012)
		fputc(015, myfiles[fd].file);
	return fputc(c, myfiles[fd].file);
}

static char*
myfgets(char *buf, int len, int fd)
{
	int c;
	char *p;

	p = buf;
	len--;	// NUL byte
	while(len--){
		c = myfgetc(fd);
		if(c == EOF){
			if(p == buf)
				return nil;
			break;
		}
		*p++ = c;
		if(c == '\n')
			break;
	}
	*p = '\0';
	return buf;
}

static int
myfread(void *buf, size_t elt, size_t n, int fd)
{
	if(myfiles[fd].isText){
		char *p;
		size_t i;
		int c;

		n *= elt;
		p = (char*)buf;
		for(i = 0; i < n; i++){
			c = myfgetc(fd);
			if(c == EOF)
				break;
			*p++ = c;
		}
		return i / elt;
	}
	return fread(buf, elt, n, myfiles[fd].file);
}

static int
myfwrite(void *buf, size_t elt, size_t n, int fd)
{
	if(myfiles[fd].isText){
		char *p;
		size_t i;
		int c;

		n *= elt;
		p = (char*)buf;
		for(i = 0; i < n; i++){
			c = *p++;
			myfputc(c, fd);
			if(feof(myfiles[fd].file))	// is this right?
				break;
		}
		return i / elt;
	}
	return fwrite(buf, elt, n, myfiles[fd].file);
}

static int
myfseek(int fd, long offset, int whence)
{
	return fseek(myfiles[fd].file, offset, whence);
}

static int
myfeof(int fd)
{
	return feof(myfiles[fd].file);
//	return ferror(myfiles[fd].file);
}


char *CFileMgr::ms_rootDirName = (char*)0x5F18F8;
char *CFileMgr::ms_dirName = (char*)0x713CA8;

void
CFileMgr::Initialise(void)
{
	_getcwd(ms_rootDirName, 128);
	strcat(ms_rootDirName, "\\");
}

void
CFileMgr::ChangeDir(const char *dir)
{
	if(*dir == '\\'){
		strcpy(ms_dirName, ms_rootDirName);
		dir++;
	}
	if(*dir != '\0'){
		strcat(ms_dirName, dir);
		// BUG in the game it seems, it's off by one
		if(dir[strlen(dir)-1] != '\\')
			strcat(ms_dirName, "\\");
	}
	chdir(ms_dirName);
}

void
CFileMgr::SetDir(const char *dir)
{
	strcpy(ms_dirName, ms_rootDirName);
	if(*dir != '\0'){
		strcat(ms_dirName, dir);
		// BUG in the game it seems, it's off by one
		if(dir[strlen(dir)-1] != '\\')
			strcat(ms_dirName, "\\");
	}
	chdir(ms_dirName);
}

void
CFileMgr::SetDirMyDocuments(void)
{
	SetDir("");	// better start at the root if user directory is relative
	chdir(_psGetUserFilesFolder());
}

int
CFileMgr::LoadFile(const char *file, uint8 *buf, int unused, const char *mode)
{
	int fd;
	int n, len;

	fd = myfopen(file, mode);
	if(fd == 0)
		return 0;
	len = 0;
	do{
		n = myfread(buf + len, 1, 0x4000, fd);
		if(n < 0)
			return -1;
		len += n;
	}while(n == 0x4000);
	buf[len] = 0;
	myfclose(fd);
	return len;
}

int
CFileMgr::OpenFile(const char *file, const char *mode)
{
	return myfopen(file, mode);
}

int
CFileMgr::OpenFileForWriting(const char *file)
{
	return OpenFile(file, "wb");
}

int
CFileMgr::Read(int fd, char *buf, int len)
{
	return myfread(buf, 1, len, fd);
}

int
CFileMgr::Write(int fd, char *buf, int len)
{
	return myfwrite(buf, 1, len, fd);
}

bool
CFileMgr::Seek(int fd, int offset, int whence)
{
	return !!myfseek(fd, offset, whence);
}

char*
CFileMgr::ReadLine(int fd, char *buf, int len)
{
	return myfgets(buf, len, fd);
}

int
CFileMgr::CloseFile(int fd)
{
	return myfclose(fd);
}

int
CFileMgr::GetErrorReadWrite(int fd)
{
	return myfeof(fd);
}

STARTPATCHES
	InjectHook(0x478F80, CFileMgr::Initialise, PATCH_JUMP);
	InjectHook(0x478FB0, CFileMgr::ChangeDir, PATCH_JUMP);
	InjectHook(0x479020, CFileMgr::SetDir, PATCH_JUMP);
	InjectHook(0x479080, CFileMgr::SetDirMyDocuments, PATCH_JUMP);
	InjectHook(0x479090, CFileMgr::LoadFile, PATCH_JUMP);
	InjectHook(0x479100, CFileMgr::OpenFile, PATCH_JUMP);
	InjectHook(0x479120, CFileMgr::OpenFileForWriting, PATCH_JUMP);
	InjectHook(0x479140, CFileMgr::Read, PATCH_JUMP);
	InjectHook(0x479160, CFileMgr::Write, PATCH_JUMP);
	InjectHook(0x479180, CFileMgr::Seek, PATCH_JUMP);
	InjectHook(0x4791D0, CFileMgr::ReadLine, PATCH_JUMP);
	InjectHook(0x479200, CFileMgr::CloseFile, PATCH_JUMP);
	InjectHook(0x479210, CFileMgr::GetErrorReadWrite, PATCH_JUMP);
ENDPATCHES