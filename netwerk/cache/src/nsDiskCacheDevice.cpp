/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is nsDiskCacheDevice.cpp, released
 * February 22, 2001.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2001
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Gordon Sheridan <gordon@netscape.com>
 *   Patrick C. Beard <beard@netscape.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include <limits.h>

// include files for ftruncate (or equivalent)
#if defined(XP_UNIX) || defined(XP_BEOS)
#include <unistd.h>
#elif defined(XP_MAC)
#include <Files.h>
#elif defined(XP_WIN)
#include <windows.h>
#elif defined(XP_OS2)
#define INCL_DOSERRORS
#include <os2.h>
#else
// XXX add necessary include file for ftruncate (or equivalent)
#endif

#include "prtypes.h"
#include "prthread.h"

#if defined(XP_MAC)
#include "pprio.h"
#else
#include "private/pprio.h"
#endif

#include "nsDiskCacheDevice.h"
#include "nsDiskCacheEntry.h"
#include "nsDiskCacheMap.h"
#include "nsDiskCacheStreams.h"

#include "nsDiskCache.h"

#include "nsCacheService.h"
#include "nsCache.h"

#include "nsDeleteDir.h"

#include "nsICacheVisitor.h"
#include "nsReadableUtils.h"
#include "nsIInputStream.h"
#include "nsIOutputStream.h"
#include "nsAutoLock.h"
#include "nsCRT.h"
#include "nsCOMArray.h"
#include "nsISimpleEnumerator.h"

static const char DISK_CACHE_DEVICE_ID[] = { "disk" };


/******************************************************************************
 *  nsDiskCacheEvictor
 *
 *  Helper class for nsDiskCacheDevice.
 *
 *****************************************************************************/
#ifdef XP_MAC
#pragma mark -
#pragma mark nsDiskCacheEvictor
#endif

class nsDiskCacheEvictor : public nsDiskCacheRecordVisitor
{
public:
    nsDiskCacheEvictor( nsDiskCacheDevice *   device,
                        nsDiskCacheMap *      cacheMap,
                        nsDiskCacheBindery *  cacheBindery,
                        PRInt32               targetSize,
                        const char *          clientID)
        : mDevice(device)
        , mCacheMap(cacheMap)
        , mBindery(cacheBindery)
        , mTargetSize(targetSize)
        , mClientID(clientID)
    {}
    
    virtual PRInt32  VisitRecord(nsDiskCacheRecord *  mapRecord);
 
private:
        nsDiskCacheDevice *  mDevice;
        nsDiskCacheMap *     mCacheMap;
        nsDiskCacheBindery * mBindery;
        PRInt32              mTargetSize;
        const char *         mClientID;
};


PRInt32
nsDiskCacheEvictor::VisitRecord(nsDiskCacheRecord *  mapRecord)
{
    // read disk cache entry
    nsDiskCacheEntry *   diskEntry = nsnull;
    nsDiskCacheBinding * binding   = nsnull;
    char *               clientID  = nsnull;
    PRInt32              result    = kVisitNextRecord;

    if (mClientID) {
         // we're just evicting records for a specific client
        nsresult  rv = mCacheMap->ReadDiskCacheEntry(mapRecord, &diskEntry);
        if (NS_FAILED(rv))  goto exit;  // XXX or delete record?
    
        // XXX FIXME compare clientID's without malloc

        // get client ID from key
        rv = ClientIDFromCacheKey(nsDependentCString(diskEntry->mKeyStart), &clientID);
        if (NS_FAILED(rv))  goto exit;
         
        if (nsCRT::strcmp(mClientID, clientID) != 0) goto exit;
    }
    
    if (mCacheMap->TotalSize() < mTargetSize) {
        result = kStopVisitingRecords;
        goto exit;
    }
    
    binding = mBindery->FindActiveBinding(mapRecord->HashNumber());
    if (binding) {
        // We are currently using this entry, so all we can do is doom it.
        // Since we're enumerating the records, we don't want to call
        // DeleteRecord when nsCacheService::DoomEntry() calls us back.
        binding->mDoomed = PR_TRUE;         // mark binding record as 'deleted'
        nsCacheService::DoomEntry(binding->mCacheEntry);
        result = kDeleteRecordAndContinue;  // this will REALLY delete the record

    } else {
        // entry not in use, just delete storage because we're enumerating the records
        (void) mCacheMap->DeleteStorage(mapRecord);
        result = kDeleteRecordAndContinue;  // this will REALLY delete the record
    }

exit:
    
    delete clientID;
    delete [] (char *)diskEntry;
    return result;
}


/******************************************************************************
 *  nsDiskCacheDeviceInfo
 *****************************************************************************/
#ifdef XP_MAC
#pragma mark -
#pragma mark nsDiskCacheDeviceInfo
#endif

class nsDiskCacheDeviceInfo : public nsICacheDeviceInfo {
public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSICACHEDEVICEINFO

    nsDiskCacheDeviceInfo(nsDiskCacheDevice* device)
        :   mDevice(device)
    {
    }

    virtual ~nsDiskCacheDeviceInfo() {}
    
private:
    nsDiskCacheDevice* mDevice;
};

NS_IMPL_ISUPPORTS1(nsDiskCacheDeviceInfo, nsICacheDeviceInfo)

/* readonly attribute string description; */
NS_IMETHODIMP nsDiskCacheDeviceInfo::GetDescription(char ** aDescription)
{
    NS_ENSURE_ARG_POINTER(aDescription);
    *aDescription = nsCRT::strdup("Disk cache device");
    return *aDescription ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
}

/* readonly attribute string usageReport; */
NS_IMETHODIMP nsDiskCacheDeviceInfo::GetUsageReport(char ** usageReport)
{
    NS_ENSURE_ARG_POINTER(usageReport);
    nsCString buffer;
    
    buffer.AssignLiteral("\n<tr>\n<td><b>Cache Directory:</b></td>\n<td><tt> ");
    nsCOMPtr<nsILocalFile> cacheDir;
    nsAutoString           path;
    mDevice->getCacheDirectory(getter_AddRefs(cacheDir)); 
    nsresult rv = cacheDir->GetPath(path);
    if (NS_SUCCEEDED(rv)) {
        AppendUTF16toUTF8(path, buffer);
    } else {
        buffer.AppendLiteral("directory unavailable");
    }
    buffer.AppendLiteral("</tt></td>\n</tr>\n");
    // buffer.Append("<tr><td><b>Files:</b></td><td><tt> XXX</tt></td></tr>");
    *usageReport = ToNewCString(buffer);
    if (!*usageReport) return NS_ERROR_OUT_OF_MEMORY;

    return NS_OK;
}

/* readonly attribute unsigned long entryCount; */
NS_IMETHODIMP nsDiskCacheDeviceInfo::GetEntryCount(PRUint32 *aEntryCount)
{
    NS_ENSURE_ARG_POINTER(aEntryCount);
    *aEntryCount = mDevice->getEntryCount();
    return NS_OK;
}

/* readonly attribute unsigned long totalSize; */
NS_IMETHODIMP nsDiskCacheDeviceInfo::GetTotalSize(PRUint32 *aTotalSize)
{
    NS_ENSURE_ARG_POINTER(aTotalSize);
    *aTotalSize = mDevice->getCacheSize();
    return NS_OK;
}

/* readonly attribute unsigned long maximumSize; */
NS_IMETHODIMP nsDiskCacheDeviceInfo::GetMaximumSize(PRUint32 *aMaximumSize)
{
    NS_ENSURE_ARG_POINTER(aMaximumSize);
    *aMaximumSize = mDevice->getCacheCapacity();
    return NS_OK;
}


/******************************************************************************
 *  nsDiskCache
 *****************************************************************************/
#ifdef XP_MAC
#pragma mark -
#pragma mark nsDiskCache
#endif

/**
 *  nsDiskCache::Hash(const char * key)
 *
 *  This algorithm of this method implies nsDiskCacheRecords will be stored
 *  in a certain order on disk.  If the algorithm changes, existing cache
 *  map files may become invalid, and therefore the kCurrentVersion needs
 *  to be revised.
 */
PLDHashNumber
nsDiskCache::Hash(const char * key)
{
    PLDHashNumber h = 0;
    for (const PRUint8* s = (PRUint8*) key; *s != '\0'; ++s)
        h = (h >> (PL_DHASH_BITS - 4)) ^ (h << 4) ^ *s;
    return (h == 0 ? ULONG_MAX : h);
}


nsresult
nsDiskCache::Truncate(PRFileDesc *  fd, PRUint32  newEOF)
{
    // use modified SetEOF from nsFileStreams::SetEOF()

#if defined(XP_UNIX) || defined(XP_BEOS)
    if (ftruncate(PR_FileDesc2NativeHandle(fd), newEOF) != 0) {
        NS_ERROR("ftruncate failed");
        return NS_ERROR_FAILURE;
    }

#elif defined(XP_MAC)
    if (::SetEOF(PR_FileDesc2NativeHandle(fd), newEOF) != 0) {
        NS_ERROR("SetEOF failed");
        return NS_ERROR_FAILURE;
    }

#elif defined(XP_WIN)
    PRInt32 cnt = PR_Seek(fd, newEOF, PR_SEEK_SET);
    if (cnt == -1)  return NS_ERROR_FAILURE;
    if (!SetEndOfFile((HANDLE) PR_FileDesc2NativeHandle(fd))) {
        NS_ERROR("SetEndOfFile failed");
        return NS_ERROR_FAILURE;
    }

#elif defined(XP_OS2)
    if (DosSetFileSize((HFILE) PR_FileDesc2NativeHandle(fd), newEOF) != NO_ERROR) {
        NS_ERROR("DosSetFileSize failed");
        return NS_ERROR_FAILURE;
    }
#else
    // add implementations for other platforms here
#endif
    return NS_OK;
}


/******************************************************************************
 *  nsDiskCacheDevice
 *****************************************************************************/

#ifdef XP_MAC
#pragma mark -
#pragma mark nsDiskCacheDevice
#endif

nsDiskCacheDevice::nsDiskCacheDevice()
    : mCacheCapacity(0)
    , mCacheMap(nsnull)
    , mInitialized(PR_FALSE)
{
}

nsDiskCacheDevice::~nsDiskCacheDevice()
{
    Shutdown();
    delete mCacheMap;
}


/**
 *  methods of nsCacheDevice
 */
nsresult
nsDiskCacheDevice::Init()
{
    nsresult rv;

    NS_ENSURE_TRUE(!Initialized(), NS_ERROR_FAILURE);
       
    if (!mCacheDirectory) return NS_ERROR_FAILURE;
    rv = mBindery.Init();
    if (NS_FAILED(rv)) return rv;
    
    // Open Disk Cache
    rv = OpenDiskCache();
    if (NS_FAILED(rv)) {
        goto error_exit;
    }

    mInitialized = PR_TRUE;
    return NS_OK;

error_exit:
    if (mCacheMap)  {
        (void) mCacheMap->Close(PR_FALSE);
        delete mCacheMap;
        mCacheMap = nsnull;
    }

    return rv;
}


/**
 *  NOTE: called while holding the cache service lock
 */
nsresult
nsDiskCacheDevice::Shutdown()
{
    return Shutdown_Private(PR_TRUE);
}


nsresult
nsDiskCacheDevice::Shutdown_Private(PRBool  flush)
{
        if (Initialized()) {
        // check cache limits in case we need to evict.
        EvictDiskCacheEntries((PRInt32)mCacheCapacity);

        // write out persistent information about the cache.
        (void) mCacheMap->Close(flush);
        delete mCacheMap;
        mCacheMap = nsnull;

        mBindery.Reset();

        mInitialized = PR_FALSE;
    }

    return NS_OK;
}


const char *
nsDiskCacheDevice::GetDeviceID()
{
    return DISK_CACHE_DEVICE_ID;
}


/**
 *  FindEntry -
 *
 *      cases:  key not in disk cache, hash number free
 *              key not in disk cache, hash number used
 *              key in disk cache
 *
 *  NOTE: called while holding the cache service lock
 */
nsCacheEntry *
nsDiskCacheDevice::FindEntry(nsCString * key, PRBool *collision)
{
    if (!Initialized())  return nsnull;  // NS_ERROR_NOT_INITIALIZED
    nsresult                rv;
    nsDiskCacheRecord       record;
    nsCacheEntry *          entry   = nsnull;
    nsDiskCacheBinding *    binding = nsnull;
    PLDHashNumber           hashNumber = nsDiskCache::Hash(key->get());

    *collision = PR_FALSE;

#if DEBUG  /*because we shouldn't be called for active entries */
    binding = mBindery.FindActiveBinding(hashNumber);
    NS_ASSERTION(!binding, "FindEntry() called for a bound entry.");
    binding = nsnull;
#endif
    
    // lookup hash number in cache map
    rv = mCacheMap->FindRecord(hashNumber, &record);
    if (NS_FAILED(rv))  return nsnull;  // XXX log error?
    
    nsDiskCacheEntry * diskEntry;
    rv = mCacheMap->ReadDiskCacheEntry(&record, &diskEntry);
    if (NS_FAILED(rv))  return nsnull;
    
    // compare key to be sure
    if (nsCRT::strcmp(diskEntry->mKeyStart, key->get()) == 0) {
        entry = diskEntry->CreateCacheEntry(this);
    } else {
        *collision = PR_TRUE;
    }
    delete [] (char *)diskEntry;
    
    // If we had a hash collision or CreateCacheEntry failed, return nsnull
    if (!entry)  return nsnull;
    
    binding = mBindery.CreateBinding(entry, &record);
    if (!binding) {
        delete entry;
        return nsnull;
    }
    
    return entry;
}


/**
 *  NOTE: called while holding the cache service lock
 */
nsresult
nsDiskCacheDevice::DeactivateEntry(nsCacheEntry * entry)
{
    nsresult              rv = NS_OK;
    nsDiskCacheBinding * binding = GetCacheEntryBinding(entry);
    NS_ASSERTION(binding, "DeactivateEntry: binding == nsnull");
    if (!binding)  return NS_ERROR_UNEXPECTED;

    if (entry->IsDoomed()) {
        // delete data, entry, record from disk for entry
        rv = mCacheMap->DeleteStorage(&binding->mRecord);

    } else {
        // save stuff to disk for entry
        rv = mCacheMap->WriteDiskCacheEntry(binding);
        if (NS_FAILED(rv)) {
            // clean up as best we can
            (void) mCacheMap->DeleteRecordAndStorage(&binding->mRecord);
            binding->mDoomed = PR_TRUE; // record is no longer in cache map
        }
    }

    mBindery.RemoveBinding(binding); // extract binding from collision detection stuff
    delete entry;   // which will release binding
    return rv;
}


/**
 * BindEntry()
 *      no hash number collision -> no problem
 *      collision
 *          record not active -> evict, no problem
 *          record is active
 *              record is already doomed -> record shouldn't have been in map, no problem
 *              record is not doomed -> doom, and replace record in map
 *              
 *              walk matching hashnumber list to find lowest generation number
 *              take generation number from other (data/meta) location,
 *                  or walk active list
 *
 *  NOTE: called while holding the cache service lock
 */
nsresult
nsDiskCacheDevice::BindEntry(nsCacheEntry * entry)
{
    if (!Initialized())  return  NS_ERROR_NOT_INITIALIZED;
    nsresult rv = NS_OK;
    nsDiskCacheRecord record, oldRecord;
    
    // create a new record for this entry
    record.SetHashNumber(nsDiskCache::Hash(entry->Key()->get()));
    record.SetEvictionRank(ULONG_MAX - SecondsFromPRTime(PR_Now()));

    if (!entry->IsDoomed()) {
        // if entry isn't doomed, add it to the cache map
        rv = mCacheMap->AddRecord(&record, &oldRecord); // deletes old record, if any
        if (NS_FAILED(rv))  return rv;
        
        PRUint32    oldHashNumber = oldRecord.HashNumber();
        if (oldHashNumber) {
            // gotta evict this one first
            nsDiskCacheBinding * oldBinding = mBindery.FindActiveBinding(oldHashNumber);
            if (oldBinding) {
                // XXX if debug : compare keys for hashNumber collision

                if (!oldBinding->mCacheEntry->IsDoomed()) {
                // we've got a live one!
                    nsCacheService::DoomEntry(oldBinding->mCacheEntry);
                    // storage will be delete when oldBinding->mCacheEntry is Deactivated
                }
            } else {
                // delete storage
                // XXX if debug : compare keys for hashNumber collision
                rv = mCacheMap->DeleteStorage(&oldRecord);
                if (NS_FAILED(rv))  return rv;  // XXX delete record we just added?
            }
        }
    }
    
    // Make sure this entry has its associated nsDiskCacheBinding attached.
    nsDiskCacheBinding *  binding = mBindery.CreateBinding(entry, &record);
    NS_ASSERTION(binding, "nsDiskCacheDevice::BindEntry");
    if (!binding) return NS_ERROR_OUT_OF_MEMORY;
    NS_ASSERTION(binding->mRecord.ValidRecord(), "bad cache map record");

    return NS_OK;
}


/**
 *  NOTE: called while holding the cache service lock
 */
void
nsDiskCacheDevice::DoomEntry(nsCacheEntry * entry)
{
    nsDiskCacheBinding * binding = GetCacheEntryBinding(entry);
    NS_ASSERTION(binding, "DoomEntry: binding == nsnull");
    if (!binding)  return;

    if (!binding->mDoomed) {
        // so it can't be seen by FindEntry() ever again.
        nsresult rv = mCacheMap->DoomRecord(&binding->mRecord);
        NS_ASSERTION(NS_SUCCEEDED(rv),"DoomRecord failed.");
        binding->mDoomed = PR_TRUE; // record in no longer in cache map
    }
}


/**
 *  NOTE: called while holding the cache service lock
 */
nsresult
nsDiskCacheDevice::OpenInputStreamForEntry(nsCacheEntry *      entry,
                                           nsCacheAccessMode   mode, 
                                           PRUint32            offset,
                                           nsIInputStream **   result)
{
    NS_ENSURE_ARG_POINTER(entry);
    NS_ENSURE_ARG_POINTER(result);

    nsresult             rv;
    nsDiskCacheBinding * binding = GetCacheEntryBinding(entry);
    NS_ENSURE_TRUE(binding, NS_ERROR_UNEXPECTED);
    
    NS_ASSERTION(binding->mCacheEntry == entry, "binding & entry don't point to each other");

    rv = binding->EnsureStreamIO();
    if (NS_FAILED(rv)) return rv;

    return binding->mStreamIO->GetInputStream(offset, result);
}


/**
 *  NOTE: called while holding the cache service lock
 */
nsresult
nsDiskCacheDevice::OpenOutputStreamForEntry(nsCacheEntry *      entry,
                                            nsCacheAccessMode   mode, 
                                            PRUint32            offset,
                                            nsIOutputStream **  result)
{
    NS_ENSURE_ARG_POINTER(entry);
    NS_ENSURE_ARG_POINTER(result);

    nsresult             rv;
    nsDiskCacheBinding * binding = GetCacheEntryBinding(entry);
    NS_ENSURE_TRUE(binding, NS_ERROR_UNEXPECTED);
    
    NS_ASSERTION(binding->mCacheEntry == entry, "binding & entry don't point to each other");

    rv = binding->EnsureStreamIO();
    if (NS_FAILED(rv)) return rv;

    return binding->mStreamIO->GetOutputStream(offset, result);
}


/**
 *  NOTE: called while holding the cache service lock
 */
nsresult
nsDiskCacheDevice::GetFileForEntry(nsCacheEntry *    entry,
                                   nsIFile **        result)
{
    NS_ENSURE_ARG_POINTER(result);
    *result = nsnull;

    nsresult             rv;
        
    nsDiskCacheBinding * binding = GetCacheEntryBinding(entry);
    if (!binding) {
        NS_WARNING("GetFileForEntry: binding == nsnull");
        return NS_ERROR_UNEXPECTED;
    }
    
    // check/set binding->mRecord for separate file, sync w/mCacheMap
    if (binding->mRecord.DataLocationInitialized()) {
        if (binding->mRecord.DataFile() != 0)
            return NS_ERROR_NOT_AVAILABLE;  // data not stored as separate file

        NS_ASSERTION(binding->mRecord.DataFileGeneration() == binding->mGeneration, "error generations out of sync");
    } else {
        binding->mRecord.SetDataFileGeneration(binding->mGeneration);
        binding->mRecord.SetDataFileSize(0);    // 1k minimum
        if (!binding->mDoomed) {
            // record stored in cache map, so update it
            rv = mCacheMap->UpdateRecord(&binding->mRecord);
            if (NS_FAILED(rv))  return rv;
        }
    }
    
    nsCOMPtr<nsIFile>  file;
    rv = mCacheMap->GetFileForDiskCacheRecord(&binding->mRecord,
                                              nsDiskCache::kData,
                                              getter_AddRefs(file));
    if (NS_FAILED(rv))  return rv;
    
    NS_IF_ADDREF(*result = file);
    return NS_OK;
}


/**
 *  This routine will get called every time an open descriptor is written to.
 *
 *  NOTE: called while holding the cache service lock
 */
nsresult
nsDiskCacheDevice::OnDataSizeChange(nsCacheEntry * entry, PRInt32 deltaSize)
{
    nsDiskCacheBinding * binding = GetCacheEntryBinding(entry);
    NS_ASSERTION(binding, "OnDataSizeChange: binding == nsnull");
    if (!binding)  return NS_ERROR_UNEXPECTED;

    NS_ASSERTION(binding->mRecord.ValidRecord(), "bad record");

    PRUint32  newSize = entry->DataSize() + deltaSize;
    PRUint32  maxSize = PR_MIN(mCacheCapacity / 2, kMaxDataFileSize);
    if (newSize > maxSize) {
        nsresult rv = nsCacheService::DoomEntry(entry);
        NS_ASSERTION(NS_SUCCEEDED(rv),"DoomEntry() failed.");
        return NS_ERROR_ABORT;
    }

    PRUint32  sizeK = ((entry->DataSize() + 0x03FF) >> 10); // round up to next 1k
    PRUint32  newSizeK =  ((newSize + 0x3FF) >> 10);

    NS_ASSERTION(sizeK < USHRT_MAX, "data size out of range");
    NS_ASSERTION(newSizeK < USHRT_MAX, "data size out of range");

    // pre-evict entries to make space for new data
    PRInt32  targetCapacity = (PRInt32)(mCacheCapacity - ((newSizeK - sizeK) * 1024));
    EvictDiskCacheEntries(targetCapacity);
    
    return NS_OK;
}


/******************************************************************************
 *  EntryInfoVisitor
 *****************************************************************************/
class EntryInfoVisitor : public nsDiskCacheRecordVisitor
{
public:
    EntryInfoVisitor(nsDiskCacheDevice * device,
                     nsDiskCacheMap *    cacheMap,
                     nsICacheVisitor *   visitor)
        : mDevice(device)
        , mCacheMap(cacheMap)
        , mVisitor(visitor)
        , mResult(NS_OK)
    {}
    
    virtual PRInt32  VisitRecord(nsDiskCacheRecord *  mapRecord)
    {
        // XXX optimization: do we have this record in memory?
        
        // read in the entry (metadata)
        nsDiskCacheEntry * diskEntry;
        nsresult rv = mCacheMap->ReadDiskCacheEntry(mapRecord, &diskEntry);
        if (NS_FAILED(rv)) {
            mResult = rv;
            return kVisitNextRecord;
        }

        // create nsICacheEntryInfo
        nsDiskCacheEntryInfo * entryInfo = new nsDiskCacheEntryInfo(DISK_CACHE_DEVICE_ID, diskEntry);
        if (!entryInfo) {
            mResult = NS_ERROR_OUT_OF_MEMORY;
            return kStopVisitingRecords;
        }
        nsCOMPtr<nsICacheEntryInfo> ref(entryInfo);
        
        PRBool  keepGoing;
        rv = mVisitor->VisitEntry(DISK_CACHE_DEVICE_ID, entryInfo, &keepGoing);
        delete [] (char *)diskEntry;
        return keepGoing ? kVisitNextRecord : kStopVisitingRecords;
    }
 
private:
        nsDiskCacheDevice * mDevice;
        nsDiskCacheMap *    mCacheMap;
        nsICacheVisitor *   mVisitor;
        nsresult            mResult;
};


nsresult
nsDiskCacheDevice::Visit(nsICacheVisitor * visitor)
{
    if (!Initialized())  return NS_ERROR_NOT_INITIALIZED;
    nsDiskCacheDeviceInfo* deviceInfo = new nsDiskCacheDeviceInfo(this);
    nsCOMPtr<nsICacheDeviceInfo> ref(deviceInfo);
    
    PRBool keepGoing;
    nsresult rv = visitor->VisitDevice(DISK_CACHE_DEVICE_ID, deviceInfo, &keepGoing);
    if (NS_FAILED(rv)) return rv;
    
    if (keepGoing) {
        EntryInfoVisitor  infoVisitor(this, mCacheMap, visitor);
        return mCacheMap->VisitRecords(&infoVisitor);
    }

    return NS_OK;
}


nsresult
nsDiskCacheDevice::EvictEntries(const char * clientID)
{
    if (!Initialized())  return NS_ERROR_NOT_INITIALIZED;
    nsresult  rv;

    if (clientID == nsnull) {
        // we're clearing the entire disk cache
        rv = ClearDiskCache();
        if (rv != NS_ERROR_CACHE_IN_USE)
            return rv;
    }

    nsDiskCacheEvictor  evictor(this, mCacheMap, &mBindery, 0, clientID);
    rv = mCacheMap->VisitRecords(&evictor);
    
    if (clientID == nsnull)     // we tried to clear the entire cache
        rv = mCacheMap->Trim(); // so trim cache block files (if possible)
    return rv;
}


/**
 *  private methods
 */
#ifdef XP_MAC
#pragma mark -
#pragma mark PRIVATE METHODS
#endif


nsresult
nsDiskCacheDevice::OpenDiskCache()
{
    nsresult  rv;

    // Try opening cache map file.
    NS_ASSERTION(mCacheMap == nsnull, "leaking mCacheMap");
    mCacheMap = new nsDiskCacheMap;
    if (!mCacheMap)
        return NS_ERROR_OUT_OF_MEMORY;
    
    // if we don't have a cache directory, create one and open it
    PRBool exists;
    rv = mCacheDirectory->Exists(&exists);
    if (NS_FAILED(rv))
        return rv;

    PRBool trashing = PR_FALSE;
    if (exists) {
        rv = mCacheMap->Open(mCacheDirectory);        
        // move "corrupt" caches to trash
        if (rv == NS_ERROR_FILE_CORRUPTED) {
            rv = DeleteDir(mCacheDirectory, PR_TRUE);
            if (NS_FAILED(rv))
                return rv;
            exists = PR_FALSE;
            trashing = PR_TRUE;
        }
        else if (NS_FAILED(rv))
            return rv;
    }

    // if we don't have a cache directory, create one and open it
    if (!exists) {
        rv = InitializeCacheDirectory();
        if (NS_FAILED(rv))
            return rv;
    }

    if (!trashing) {
        // delete any trash files leftover from a previous run
        nsCOMPtr<nsIFile> trashDir;
        GetTrashDir(mCacheDirectory, &trashDir);
        if (trashDir) {
            PRBool exists;
            if (NS_SUCCEEDED(trashDir->Exists(&exists)) && exists)
                DeleteDir(trashDir, PR_FALSE);
        }
    }

    return NS_OK;
}


nsresult
nsDiskCacheDevice::ClearDiskCache()
{
    if (mBindery.ActiveBindings())
        return NS_ERROR_CACHE_IN_USE;

    nsresult rv = Shutdown_Private(PR_FALSE);  // false: don't bother flushing
    if (NS_FAILED(rv))
        return rv;

    rv = DeleteDir(mCacheDirectory, PR_TRUE);
    if (NS_FAILED(rv))
        return rv;

    return Init();
}


nsresult
nsDiskCacheDevice::InitializeCacheDirectory()
{
    nsresult rv;
    
    rv = mCacheDirectory->Create(nsIFile::DIRECTORY_TYPE, 0777);
    CACHE_LOG_PATH(PR_LOG_ALWAYS, "\ncreate cache directory: %s\n", mCacheDirectory);
    CACHE_LOG_ALWAYS(("mCacheDirectory->Create() = %x\n", rv));
    if (NS_FAILED(rv))  return rv;

    // reopen the cache map     
    rv = mCacheMap->Open(mCacheDirectory);
    return rv;
}


nsresult
nsDiskCacheDevice::EvictDiskCacheEntries(PRInt32  targetCapacity)
{
    nsresult rv;
    
    if (mCacheMap->TotalSize() < targetCapacity)  return NS_OK;

    nsDiskCacheEvictor  evictor(this, mCacheMap, &mBindery, targetCapacity, nsnull);
    rv = mCacheMap->EvictRecords(&evictor);
    
    return rv;
}


/**
 *  methods for prefs
 */
#ifdef XP_MAC
#pragma mark -
#pragma mark PREF METHODS
#endif

void
nsDiskCacheDevice::SetCacheParentDirectory(nsILocalFile * parentDir)
{
    nsresult rv;
    PRBool  exists;

    if (Initialized()) {
        NS_ASSERTION(PR_FALSE, "Cannot switch cache directory when initialized");
        return;
    }

    if (!parentDir) {
        mCacheDirectory = nsnull;
        return;
    }

    // ensure parent directory exists
    rv = parentDir->Exists(&exists);
    if (NS_SUCCEEDED(rv) && !exists)
        rv = parentDir->Create(nsIFile::DIRECTORY_TYPE, 0700);
    if (NS_FAILED(rv))  return;

    // ensure cache directory exists
    nsCOMPtr<nsIFile> directory;
    
    rv = parentDir->Clone(getter_AddRefs(directory));
    if (NS_FAILED(rv))  return;
    rv = directory->AppendNative(NS_LITERAL_CSTRING("Cache"));
    if (NS_FAILED(rv))  return;
    
    mCacheDirectory = do_QueryInterface(directory);
}


void
nsDiskCacheDevice::getCacheDirectory(nsILocalFile ** result)
{
    *result = mCacheDirectory;
    NS_IF_ADDREF(*result);
}


/**
 *  NOTE: called while holding the cache service lock
 */
void
nsDiskCacheDevice::SetCapacity(PRUint32  capacity)
{
    mCacheCapacity = capacity * 1024;
    if (Initialized()) {
        // start evicting entries if the new size is smaller!
        EvictDiskCacheEntries((PRInt32)mCacheCapacity);
    }
}


PRUint32 nsDiskCacheDevice::getCacheCapacity()
{
    return mCacheCapacity;
}


PRUint32 nsDiskCacheDevice::getCacheSize()
{
    return mCacheMap->TotalSize();
}


PRUint32 nsDiskCacheDevice::getEntryCount()
{
    return mCacheMap->EntryCount();
}
