/*
** 2004 May 22
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
** This header file defined OS-specific features for OS/2.
*/
#ifndef _SQLITE_OS_OS2_H_
#define _SQLITE_OS_OS2_H_

/*
** standard include files.
*/
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>


/*
** The OsFile structure is a operating-system independing representation
** of an open file handle.  It is defined differently for each architecture.
**
** This is the definition for Unix.
**
** OsFile.locktype takes one of the values SHARED_LOCK, RESERVED_LOCK,
** PENDING_LOCK or EXCLUSIVE_LOCK.
*/
typedef struct OsFile OsFile;
struct OsFile {
     unsigned long h;        /* The file descriptor (LHANDLE) */
     int locked;              /* True if this user holds the lock */
     int delOnClose;          /* True if file is to be deleted on close */
     char *pathToDel;         /* Name of file to delete on close */
     unsigned char locktype;   /* The type of lock held on this fd */
     unsigned char isOpen;   /* True if needs to be closed */
     unsigned char fullSync; 
};

/*
** Maximum number of characters in a temporary file name
*/
#define SQLITE_TEMPNAME_SIZE 200

/*
** Minimum interval supported by sqlite3OsSleep().
*/
#define SQLITE_MIN_SLEEP_MS 1

#endif /* _SQLITE_OS_OS2_H_ */
