/**Library and standard headers**/
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <grp.h>
#include <pwd.h>
#include <assert.h>

/**GDRIVE HEADERS***************/
#include "fuse-drive.h"
#include "gdrive/gdrive-util.h"
#include "gdrive/gdrive.h"
#include "fuse-drive-options.h"

/**Function Prototypes*********/
//1:Aditya
//2:Shubhika
//3:Swarnika
//4:Tanu
static int set_fileinfo(const Gdrive_Fileinfo* pFileinfo,bool isRoot, struct stat* stbuf);//1

static int remove_by_id(const char* fileId, const char* parentId);//2

static unsigned int get_max_permissions(bool isDir);//1

static bool match_user_to_group(gid_t gidToMatch, gid_t gid, uid_t uid);//2

static int check_access(const char* path, int mask);//3

static int create_file(const char* path, mode_t mode,struct fuse_file_info* fi);//1

static void destroy_link(void* private_data);

static int get_file_attr(const char* path, struct stat* stbuf, struct fuse_file_info* fi);//1

static int sync_file(const char* path, int isdatasync,struct fuse_file_info* fi);//2

static int truncate_file(const char* path, off_t size,struct fuse_file_info* fi);//4

static int get_attr(const char *path, struct stat *stbuf);//1

static void* init_fuse(struct fuse_conn_info *conn);//3

static int link_file(const char* from, const char* to);//2

static int make_dir(const char* path, mode_t mode);//2

static int open_file(const char *path, struct fuse_file_info *fi);//1

static int read_file(const char *path, char *buf, size_t size, off_t offset,struct fuse_file_info *fi);//2

static int read_dir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);//4

static int release_file(const char* path, struct fuse_file_info *fi);//4

static int rename_file_or_dir(const char* from, const char* to);//4

static int remove_dir(const char* path);//3

static int get_filesys_stats(const char* path, struct statvfs* stbuf);//3

static int trunc(const char* path, off_t size);//3

static int unlink_file(const char* path);//4

static int access_time_change(const char* path, const struct timespec ts[2]);//4

static int write_file(const char* path, const char *buf, size_t size,off_t offset, struct fuse_file_info* fi);//3


/**
 * The set_fileinfo function fetches the required file information
 * from the pFileinfo structure object
 * and adds it to the stat structure array named stbuf
 * */
static int set_fileinfo(const Gdrive_Fileinfo* pFileinfo,bool isRoot, struct stat* stbuf)
{
    switch (pFileinfo->type)
    {
        case GDRIVE_FILETYPE_FOLDER:
            stbuf->st_mode = S_IFDIR; //directory mode
            stbuf->st_nlink = pFileinfo->nParents + pFileinfo->nChildren;
            // Account for ".".  Also, if the root of the filesystem, account
            // for  "..", which is outside of the Google Drive filesystem and
            // thus not included in nParents.
            stbuf->st_nlink += isRoot ? 2 : 1;  //set if root directory or not
            break;

        case GDRIVE_FILETYPE_FILE:
            // Fall through to default
            default:
            {
                stbuf->st_mode = S_IFREG;
                stbuf->st_nlink = pFileinfo->nParents;
            }
    }

    unsigned int perms = gdrive_finfo_real_perms(pFileinfo);      //get file permissions
    unsigned int maxPerms =
        get_max_permissions(pFileinfo->type == GDRIVE_FILETYPE_FOLDER);//get max file permissions possible
    // Owner permissions.
    stbuf->st_mode = stbuf->st_mode | ((perms << 6) & maxPerms);
    // Group permissions
    stbuf->st_mode = stbuf->st_mode | ((perms << 3) & maxPerms);
    // User permissions
    stbuf->st_mode = stbuf->st_mode | ((perms) & maxPerms);

    stbuf->st_uid = geteuid();//user id
    stbuf->st_gid = getegid();//group id
    stbuf->st_size = pFileinfo->size; //size of file
    stbuf->st_atime = pFileinfo->accessTime.tv_sec; //access time in sec
    stbuf->st_atim.tv_nsec = pFileinfo->accessTime.tv_nsec;//access time in nano sec
    stbuf->st_mtime = pFileinfo->modificationTime.tv_sec;//modification time
    stbuf->st_mtim.tv_nsec = pFileinfo->modificationTime.tv_nsec;//modification time in sec
    stbuf->st_ctime = pFileinfo->creationTime.tv_sec;//creation time in sec
    stbuf->st_ctim.tv_nsec = pFileinfo->creationTime.tv_nsec;//creation time in nano sec

    return 0;
}
/** function to remove a file or directory when id of that file or directory is given
 * find the number of hard links i.e, parents of that file, it gets the info of the file using func gdrive_finfo_get_by_id
 * (which is in file gdrive_fileinfo.c) and then if the result is not null that is file exists then no. of parents are found
 * if multiple parents then remove parents..
 **/
static int remove_by_id(const char* fileId, const char* parentId)
{
    // The fileId should never be NULL. A NULL parentId is a runtime error, but
    // it shouldn't stop execution. Just check the fileId here.
    assert(fileId != NULL);

    // Find the number of parents, which is the number of "hard" links.
    const Gdrive_Fileinfo* pFileinfo = gdrive_finfo_get_by_id(fileId);
    if (pFileinfo->nParents > 1)
    {
        return gdrive_remove_parent(fileId, parentId);
    }
    // else this is the only hard link. Delete or trash the file.

    return gdrive_delete(fileId, parentId);
}


/**
 * This function returns the maximum permissions a user can get on a file or a directory
 * This remains static for every file or directory.
 * */
static unsigned int get_max_permissions(bool isDir)
{
    struct fuse_context* context = fuse_get_context();
    unsigned long perms = (unsigned long) context->private_data;
    if (isDir)
    {
        perms >>= 9;
    }
    else
    {
        perms &= 0777;
    }

    return perms & ~context->umask;
}

/**function checks whether the user's id stored in gidToMatch is a valid user that has permission to access a file
 *The getpwuid() function shall search the user database for an entry with a matching uid.
 * The getgrgid() function shall search the group database for an entry with a matching gid.
 * both function returns pointer to struct group, structure defined in grp.h and returns null pointer if entry not found or error
 * **gr_mem points to null terminated array of character pointer to member names i.e.assigned to pName
 * for loop does the string matching to match id's  **/
static bool match_user_to_group(gid_t gidToMatch, gid_t gid, uid_t uid)
{
    // Simplest case - primary group matches
    if (gid == gidToMatch)
    {
        return true;
    }

    // Get a list of all the users in the group, and see whether the desired
    // user is in the list. It seems like there MUST be a cleaner way to do
    // this!
    const struct passwd* userInfo = getpwuid(uid);
    const struct group* grpInfo = getgrgid(gidToMatch);
    for (char** pName = grpInfo->gr_mem; *pName; pName++)
    {
        if (!strcmp(*pName, userInfo->pw_name))
        {
            return true;
        }
    }

    // No match found
    return false;
}


/**
 * This function gets the file id from the path passed ,
 * then it retrieves the file info by calling gdrive_finfo_get_by_id function()
 * from the fileinfo we get the file permissions  and we also fetch the maximum file permissions using get_max_perms () function.
 * We fetch the system permissions as well and accordingly grant access (return 0) to the calling method.
 *
 * */
static int check_access(const char* path, int mask)
{


    char* fileId = gdrive_filepath_to_id(path);
    if (!fileId)
    {
        // File doesn't exist
        return -ENOENT;
    }
    const Gdrive_Fileinfo* pFileinfo = gdrive_finfo_get_by_id(fileId);
    free(fileId);
    if (!pFileinfo)
    {
        // Unknown error
        return -EIO;
    }

    if (mask == F_OK)
    {
        // Only checking whether the file exists
        return 0;
    }

    unsigned int filePerms = gdrive_finfo_real_perms(pFileinfo);
    unsigned int maxPerms =
        get_max_permissions(pFileinfo->type == GDRIVE_FILETYPE_FOLDER);

    const struct fuse_context* context = fuse_get_context();

    if (context->uid == geteuid())
    {
        // User permission
        maxPerms >>= 6;
    }
    else if (match_user_to_group(getegid(), context->gid, context->uid))
    {
        // Group permission
        maxPerms >>= 3;
    }
    // else other permission, don't change maxPerms

    unsigned int finalPerms = filePerms & maxPerms;

    if (((mask & R_OK) && !(finalPerms & S_IROTH)) ||
            ((mask & W_OK) && !(finalPerms & S_IWOTH)) ||
            ((mask & X_OK) && !(finalPerms & S_IXOTH))
            )
    {
        return -EACCES;
    }

    return 0;
}
/**
 * This function checks whether the file name already exists in this same path,
 * if so, it returns an error
 * else it checks for the write access to the parent directory using access() function and then
 * creats a new file with the required name using the gdrive_file_new() function
 * */
static int create_file(const char* path, mode_t mode, struct fuse_file_info* fi)
{

    /** Determine whether the file already exists **/
    char* tempFileId = gdrive_filepath_to_id(path);
    free(tempFileId);
    /**Need write access to the parent directory  **/
    Gdrive_Path* pGpath = gdrive_path_create(path);
    int accessResult = check_access(gdrive_path_get_dirname(pGpath), W_OK);
    gdrive_path_free(pGpath);
    if (accessResult)
    {
        // Access check failed
        return accessResult;
    }

    /**Create the file  **/
    int error = 0;
    char* fileId = gdrive_file_new(path, false, &error);

    /** File was successfully created. Open it.**/
    fi->fh = (uint64_t) gdrive_file_open(fileId, O_RDWR, &error);
    free(fileId);

    return -error;
}


/* This function destroys or cleans the complete gdrive
*/
static void destroy_link(void* private_data)
{
    // Silence compiler warning about unused parameter
    (void) private_data;

    gdrive_cleanup();
}

/*This function is used to get attributes from an open file This method is called instead of the getattr() method if the file information is available.*/
static int get_file_attr(const char* path, struct stat* stbuf,
                         struct fuse_file_info* fi)
{
    Gdrive_File* fh = (Gdrive_File*) fi->fh;
    const Gdrive_Fileinfo* pFileinfo = (fi->fh == (uint64_t) NULL) ?
        NULL : gdrive_file_get_info(fh);

    if (pFileinfo == NULL)
    {
        // Invalid file handle
        return -EBADF;
    }

    return set_fileinfo(pFileinfo, strcmp(path, "/") == 0, stbuf);
}

/**This function calls for gdrive_file_sync() function to sync the metadata of file with google drive
 * gdrive_file_sync():  Sync a file's metadata (the information stored in a
 *                      Gdrive_Fileinfo struct) with Google Drive.
 * */
static int sync_file(const char* path, int isdatasync,struct fuse_file_info* fi)
{

    (void) isdatasync;
    (void) path;
    /** check to see if file handle is NULL**/
    if (fi->fh == (uint64_t) NULL)
    {
        // Bad file handle
        return -EBADF;
    }

    return gdrive_file_sync((Gdrive_File*) fi->fh);
}
/**the file is given write access first by check_access function, which returns 0 on successful completion
 * and then function returns control to gdrive_file_truncate which is in gdrive_file.h file, this functio
 * Changes the size of an open file. If increasing the size, the end of the file will be
 * filled with null  bytes.
 *  pFile (Gdrive_File*):A file handle returned by a prior call to gdrive_file_open().
 * function returns 0 on success else -ve int
 * */
static int truncate_file(const char* path, off_t size,
                          struct fuse_file_info* fi)
{
    // Suppress unused parameter compiler warnings
    (void) path;

    Gdrive_File* fh = (Gdrive_File*) fi->fh;
    // Need write access to the file
    int accessResult = check_access(path, W_OK);
    if (accessResult)
    {
        return accessResult;
    }

    return gdrive_file_truncate(fh, size);
}
/**gdrive_filepath_to_id() is in gdrive-info.c, gdrive_finfo_get_by_id() is in gdrive-fileinfo.c,
 * set_fileinfo is in this file only
 * **/
static int get_attr(const char *path, struct stat *stbuf)
{
    memset(stbuf, 0, sizeof(struct stat));

    char* fileId = gdrive_filepath_to_id(path);
    if (fileId == NULL)
    {
        // File not found
        return -ENOENT;
    }

    const Gdrive_Fileinfo* pFileinfo = gdrive_finfo_get_by_id(fileId);
    free(fileId);
    if (pFileinfo == NULL)
    {
        // An error occurred.
        return -ENOENT;
    }

    return set_fileinfo(pFileinfo, strcmp(path, "/") == 0, stbuf);
}

/*This function is used to initialize filesystem
The return value will passed in the private_data field of fuse_context to all file operations and as a parameter to the destroy() method.*/
static void* init_fuse(struct fuse_conn_info *conn)
{
    // Add any desired capabilities.
    conn->want = conn->want |
            FUSE_CAP_ATOMIC_O_TRUNC | FUSE_CAP_BIG_WRITES |
            FUSE_CAP_EXPORT_SUPPORT;
    // Remove undesired capabilities.
    conn->want = conn->want & !(FUSE_CAP_ASYNC_READ);

    // Need to turn off async read here, too.
    conn->async_read = 0;

    return fuse_get_context()->private_data;
}

//This function is used to create a hard link to a file
static int link_file(const char* from, const char* to)
{
    Gdrive_Path* pOldPath = gdrive_path_create(from);
    Gdrive_Path* pNewPath = gdrive_path_create(to);

    // Determine whether the file already exists
    char* dummyFileId = gdrive_filepath_to_id(to);
    free(dummyFileId);
    if (dummyFileId != NULL)
    {
        return -EEXIST;
    }

    // Google Drive supports a file with multiple parents - that is, a file with
    // multiple hard links that all have the same base name.
    if (strcmp(gdrive_path_get_basename(pOldPath),
               gdrive_path_get_basename(pNewPath))
            )
    {
        // Basenames differ, not supported
        return -ENOENT;
    }

    // Need write access in the target directory
    int accessResult = check_access(gdrive_path_get_dirname(pNewPath), W_OK);
    if (accessResult)
    {
        return accessResult;
    }

    char* fileId = gdrive_filepath_to_id(from);
    if (!fileId)
    {
        // Original file does not exist
        return -ENOENT;
    }
    char* newParentId =
        gdrive_filepath_to_id(gdrive_path_get_dirname(pNewPath));
    gdrive_path_free(pOldPath);
    gdrive_path_free(pNewPath);
    if (!newParentId)
    {
        // New directory doesn't exist
        free(fileId);
        return -ENOENT;
    }

    int returnVal = gdrive_add_parent(fileId, newParentId);

    free(fileId);
    free(newParentId);
    return returnVal;
}
/**
 * This function checks for the read permissions for the file to be read
 * If read permissions are granted then we get the file handle and read the file using gdrive_file_read() function
 * */
static int read_file(const char *path, char *buf, size_t size, off_t offset,struct fuse_file_info *fi)
{
    /** Check for read access **/
    int accessResult = check_access(path, R_OK);
    if (accessResult)
    {
        return accessResult;
    }

    Gdrive_File* pFile = (Gdrive_File*) fi->fh;

    return gdrive_file_read(pFile, buf, size, offset);
}

/**
 * This function checks whether the directory name already exists in this same path,
 * if so, it returns an error
 * else it gives the write access to the parent directory using access() function and then
 * creats a new directory with the required name using the gdrive_path_create() function
 * */
static int make_dir(const char* path, mode_t mode)
{
    /*// Silence compiler warning for unused variable. If and when chmod is
    // implemented, this should be removed.
    (void) mode;
    */
    /** Determine whether the folder already exists, **/
    char* tempFileId = gdrive_filepath_to_id(path);
    free(tempFileId);
    if (tempFileId != NULL)
    {
        return -EEXIST;
    }

    /**Need write access to the parent directory  **/
    Gdrive_Path* pGpath = gdrive_path_create(path);
    if (!pGpath)
    {
        // Memory error
        return -ENOMEM;
    }
    int accessResult = check_access(gdrive_path_get_dirname(pGpath), W_OK);
    gdrive_path_free(pGpath);
    if (accessResult)
    {
        return accessResult;
    }

    /** Create the folder if access is granted**/
    int error = 0;
    tempFileId = gdrive_file_new(path, true, &error);
    free(tempFileId);



    return -error;
}
/**
 * This function gets the file id using gdrive_filepath_to_id() using the file path passed as argument
 * It checks for appropriate permissions of the file.
 * It checks for the access mode for the path specifies using access() function.
 * If access is provided then the file is opened using gdrive_file_open () function.
 * */
static int open_file(const char *path, struct fuse_file_info *fi)
{
    /** Get the file ID  **/
    char* fileId = gdrive_filepath_to_id(path);
    if (fileId == NULL)
    {
        // File not found
        return -ENOENT;
    }

    /** Confirm permissions **/
    unsigned int modeNeeded = 0;
    if (fi->flags & (O_RDONLY | O_RDWR))
    {
        modeNeeded |= R_OK;
    }
    if (fi->flags & (O_WRONLY | O_RDWR))
    {
        modeNeeded |= W_OK;
    }
    if (!modeNeeded)
    {
        modeNeeded = F_OK;
    }
    int accessResult = check_access(path, modeNeeded);
    if (accessResult)
    {
        return accessResult;
    }

    /** Open the file  **/
    int error = 0;
    Gdrive_File* pFile = gdrive_file_open(fileId, fi->flags, &error);
    free(fileId);

    if (pFile == NULL)
    {
        // An error occurred.
        return -error;
    }

    /** Store the file handle **/
    fi->fh = (uint64_t) pFile;
    return 0;
}

/*This function reads the gdrive by using the filler function. It checks
* for read access first and then use the filler function to read the gdrive
*/

static int read_dir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{
    // Suppress warnings for unused function parameters
    (void) offset;
    (void) fi;

    char* folderId = gdrive_filepath_to_id(path);
    if (folderId == NULL)
    {
        return -ENOENT;
    }

    // Check for read access
    int accessResult = check_access(path, R_OK);
    if (accessResult)
    {
        free(folderId);
        return accessResult;
    }

    Gdrive_Fileinfo_Array* pFileArray =
            gdrive_folder_list(folderId);
    free(folderId);
    if (pFileArray == NULL)
    {
        // An error occurred.
        return -ENOENT;
    }

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    const Gdrive_Fileinfo* pCurrentFile;
    for (pCurrentFile = gdrive_finfoarray_get_first(pFileArray);
            pCurrentFile != NULL;
            pCurrentFile = gdrive_finfoarray_get_next(pFileArray, pCurrentFile)
            )
    {
        struct stat st = {0};
        switch (pCurrentFile->type)
        {
            case GDRIVE_FILETYPE_FILE:
                st.st_mode = S_IFREG;
                break;

            case GDRIVE_FILETYPE_FOLDER:
                st.st_mode = S_IFDIR;
                break;
        }
        filler(buf, pCurrentFile->filename, &st, 0);
    }

    gdrive_finfoarray_free(pFileArray);

    return 0;
}

/*This function is used to release an open file
Release is called when there are no more references to an open file: all file descriptors are closed and all memory mappings are unmapped.*/
static int release_file(const char* path, struct fuse_file_info* fi)
{
    // Suppress unused parameter warning
    (void) path;

    if (fi->fh == (uint64_t) NULL)
    {
        // Bad file handle
        return -EBADF;
    }

    gdrive_file_close((Gdrive_File*) fi->fh, fi->flags);
    return 0;
}

/*It renames the file. Here a lot of checks are implemented.
  It takes two arguments from and to and checks whether they are the root directory. If yes, then error is handled. Further
  checks are implemented to see if the destination folder exists or not.*/

static int rename_file_or_dir(const char* from, const char* to)
{
    // Neither from nor to should be the root directory
    char* rootId = gdrive_filepath_to_id("/");
    char* fromFileId = gdrive_filepath_to_id(from);
    if (!strcmp(fromFileId, rootId))
    {
        // from is root
        free(fromFileId);
        free(rootId);
        return -EBUSY;
    }
    char* toFileId = gdrive_filepath_to_id(to);
    // toFileId may be NULL.

    // Special handling if the destination exists
    if (toFileId)
    {
        if (!strcmp(toFileId, rootId))
        {
            // to is root
            free(toFileId);
            free(fromFileId);
            free(rootId);
            return -EBUSY;
        }
        free(rootId);

        // If from and to are hard links to the same file, do nothing and
        // return success.
        if (!strcmp(fromFileId, toFileId))
        {
            free(toFileId);
            free(fromFileId);
            return 0;
        }

        // If the source is a directory, destination must be an empty directory
        const Gdrive_Fileinfo* pFromInfo = gdrive_finfo_get_by_id(fromFileId);
        if (pFromInfo && pFromInfo->type == GDRIVE_FILETYPE_FOLDER)
        {
            const Gdrive_Fileinfo* pToInfo = gdrive_finfo_get_by_id(toFileId);
            if (pToInfo && pToInfo->type != GDRIVE_FILETYPE_FOLDER)
            {
                // Destination is not a directory
                free(toFileId);
                free(fromFileId);
                return -ENOTDIR;
            }
            if (pToInfo && pToInfo->nChildren > 0)
            {
                // Destination is not empty
                free(toFileId);
                free(fromFileId);
                return -ENOTEMPTY;
            }
        }

        // Need write access for the destination
        int accessResult = check_access(to, W_OK);
        if (accessResult)
        {
            free(toFileId);
            free(fromFileId);
            return -EACCES;
        }
    }
    else
    {
        free(rootId);
    }

    Gdrive_Path* pFromPath = gdrive_path_create(from);

    Gdrive_Path* pToPath = gdrive_path_create(to);
    char* fromParentId =
        gdrive_filepath_to_id(gdrive_path_get_dirname(pFromPath));
    if (!fromParentId)
    {
        // from path doesn't exist
        gdrive_path_free(pToPath);
        gdrive_path_free(pFromPath);
        free(fromFileId);
        return -ENOENT;
    }
    char* toParentId =
        gdrive_filepath_to_id(gdrive_path_get_dirname(pToPath));
    if (!toParentId)
    {
        // from path doesn't exist
        free(fromParentId);
        gdrive_path_free(pToPath);
        gdrive_path_free(pFromPath);
        free(fromFileId);
        return -ENOENT;
    }

    // Need write access in the destination parent directory
    int accessResult = check_access(gdrive_path_get_dirname(pToPath), W_OK);
    if (accessResult)
    {
        free(toParentId);
        free(fromParentId);
        gdrive_path_free(pToPath);
        gdrive_path_free(pFromPath);
        free(fromFileId);
        return accessResult;
    }

    // If the directories are different, create a new hard link and delete
    // the original. Compare the actual file IDs of the parents, not the paths,
    // because different paths could refer to the same directory.
    if (strcmp(fromParentId, toParentId))
    {
        int result = gdrive_add_parent(fromFileId, toParentId);
        if (result != 0)
        {
            // An error occurred
            free(toParentId);
            free(fromParentId);
            gdrive_path_free(pToPath);
            gdrive_path_free(pFromPath);
            free(fromFileId);
            return result;
        }
        result = unlink_file(from);
        if (result != 0)
        {
            // An error occurred
            free(toParentId);
            free(fromParentId);
            gdrive_path_free(pToPath);
            gdrive_path_free(pFromPath);
            free(fromFileId);
            return result;
        }
    }

    int returnVal = 0;

    // If the basenames are different, change the basename. NOTE: If there are
    // any other hard links to the file, this will also change their names.
    const char* fromBasename = gdrive_path_get_basename(pFromPath);
    const char* toBasename = gdrive_path_get_basename(pToPath);
    if (strcmp(fromBasename, toBasename))
    {
        returnVal = gdrive_change_basename(fromFileId, toBasename);
    }

    // If successful, and if to already existed, delete it
    if (toFileId && !returnVal)
    {
        returnVal = remove_by_id(toFileId, toParentId);
    }


    free(toFileId);
    free(toParentId);
    free(fromParentId);
    gdrive_path_free(pFromPath);
    gdrive_path_free(pToPath);
    free(fromFileId);
    return returnVal;
}

/*This function removes the current directory. It first checks whether the directory is root or not. If yes, then error is returned
*/

static int remove_dir(const char* path)
{
    // Can't delete the root directory
    if (strcmp(path, "/") == 0)
    {
        return -EBUSY;
    }

    char* fileId = gdrive_filepath_to_id(path);

    // Make sure path refers to an empty directory
    const Gdrive_Fileinfo* pFileinfo = gdrive_finfo_get_by_id(fileId);

    if (pFileinfo->type != GDRIVE_FILETYPE_FOLDER)
    {
        // Not a directory
        free(fileId);
        return -ENOTDIR;
    }
    if (pFileinfo->nChildren > 0)
    {
        // Not empty
        free(fileId);
        return -ENOTEMPTY;
    }

    // Need write access
    int accessResult = check_access(path, W_OK);
    if (accessResult)
    {
        free(fileId);
        return accessResult;
    }

    // Get the parent ID
    Gdrive_Path* pGpath = gdrive_path_create(path);

    char* parentId =
        gdrive_filepath_to_id(gdrive_path_get_dirname(pGpath));
    gdrive_path_free(pGpath);

    int returnVal = remove_by_id(fileId, parentId);
    free(parentId);
    free(fileId);
    return returnVal;
}

//This function is used to get file system statistics
static int get_filesys_stats(const char* path, struct statvfs* stbuf)
{
    // Suppress compiler warning about unused parameter
    (void) path;

    unsigned long blockSize = gdrive_get_minchunksize();
    unsigned long bytesTotal = gdrive_sysinfo_get_size();
    unsigned long bytesFree = bytesTotal - gdrive_sysinfo_get_used();

    memset(stbuf, 0, sizeof(statvfs));
    stbuf->f_bsize = blockSize;
    stbuf->f_blocks = bytesTotal / blockSize;
    stbuf->f_bfree = bytesFree / blockSize;
    stbuf->f_bavail = stbuf->f_bfree;

    return 0;
}

//This function is used to change the size of the file to passed size argument.
static int trunc(const char* path, off_t size)
{
    char* fileId = gdrive_filepath_to_id(path);
    if (fileId == NULL)
    {
        // File not found
        return -ENOENT;
    }

    // Need write access
    int accessResult = check_access(path, W_OK);
    if (accessResult)
    {
        free(fileId);
        return accessResult;
    }

    // Open the file
    int error = 0;
    Gdrive_File* fh = gdrive_file_open(fileId, O_RDWR, &error);
    free(fileId);
    if (fh == NULL)
    {
        // Error
        return error;
    }

    // Truncate
    int result = gdrive_file_truncate(fh, size);

    // Close
    gdrive_file_close(fh, O_RDWR);

    return result;
}
//This function is used to remove a file
static int unlink_file(const char* path)
{
    char* fileId = gdrive_filepath_to_id(path);
    if (fileId == NULL)
    {
        // No such file
        return -ENOENT;
    }

    // Need write access
    int accessResult = check_access(path, W_OK);
    if (accessResult)
    {
        free(fileId);
        return accessResult;
    }

    Gdrive_Path* pGpath = gdrive_path_create(path);
    if (pGpath == NULL)
    {
        // Memory error
        free(fileId);
        return -ENOMEM;
    }
    char* parentId = gdrive_filepath_to_id(gdrive_path_get_dirname(pGpath));
    gdrive_path_free(pGpath);

    int returnVal = remove_by_id(fileId, parentId);
    free(parentId);
    free(fileId);
    return returnVal;
}

/* This functions fetches the current time and modifies the access time of the file or directory**/
static int access_time_change(const char* path, const struct timespec ts[2])
{
    char* fileId = gdrive_filepath_to_id(path);
    if (fileId == NULL)
    {
        return -ENOENT;
    }

    int error = 0;
    Gdrive_File* fh = gdrive_file_open(fileId, O_RDWR, &error);
    free(fileId);
    if (fh == NULL)
    {
        return -error;
    }

    if (ts[0].tv_nsec == UTIME_NOW)
    {
        error = gdrive_file_set_atime(fh, NULL);
    }
    else if (ts[0].tv_nsec != UTIME_OMIT)
    {
        error = gdrive_file_set_atime(fh, &(ts[0]));
    }

    if (error != 0)
    {
        gdrive_file_close(fh, O_RDWR);
        return error;
    }

    if (ts[1].tv_nsec == UTIME_NOW)
    {
        gdrive_file_set_mtime(fh, NULL);
    }
    else if (ts[1].tv_nsec != UTIME_OMIT)
    {
        gdrive_file_set_mtime(fh, &(ts[1]));
    }

    gdrive_file_close(fh, O_RDWR);
    return error;
}
/**
 * This function checks for the write permissions of the file using access () function.
 * If write access is granted then we get the file handle and write the buffer
 *  to the file from a specified offset in the file
 * using the gdrive_file_write function()
 * */
static int write_file(const char* path, const char *buf, size_t size,off_t offset, struct fuse_file_info* fi)
{
    // Avoid compiler warning for unused variable
    (void) path;

    // Check for write access
    int accessResult = check_access(path, W_OK);
    if (accessResult)
    {
        return accessResult;
    }

    Gdrive_File* fh = (Gdrive_File*) fi->fh;
    if (fh == NULL)
    {
        // Bad file handle
        return -EBADFD;
    }

    return gdrive_file_write(fh, buf, size, offset);
}

/**setting members for fuse operations**/
static struct fuse_operations fo = {
	/**Mapping to the functions we defined, rest of the function pointers set to NULL**/
	.access         = check_access,
	.create         = create_file,
    .destroy        = destroy_link,
    .fgetattr       = get_file_attr,
    .fsync          = sync_file,
    .ftruncate      = truncate_file,
    .getattr        = get_attr,
    .link           = link_file,
    .open           = open_file,
    .mkdir          = make_dir,
    .read           = read_file,
    .readdir        = read_dir,
    .init           = init_fuse,
    .release        = release_file,
    .statfs         = get_filesys_stats,
    .rename         = rename_file_or_dir,
    .rmdir          = remove_dir,
    .truncate       = trunc,
    .unlink         = unlink_file,
    .utimens        = access_time_change,
    .write          = write_file,

//unimplemented callback functions set to NULL

    .bmap           = NULL,
    .chmod          = NULL,
    .chown          = NULL,
    .fallocate      = NULL,
    .flock          = NULL,
    .flush          = NULL,
    .fsyncdir       = NULL,
    .getxattr       = NULL,
    .ioctl          = NULL,
    .listxattr      = NULL,
    .lock           = NULL,
    .mknod          = NULL,
    .opendir        = NULL,
    .poll           = NULL,
    .read_buf       = NULL,
    .readlink       = NULL,
    .releasedir     = NULL,
    .removexattr    = NULL,
    .setxattr       = NULL,
    .symlink        = NULL,
    .utime          = NULL,
    .write_buf      = NULL,
};



int main(int argc,char *argv[])
{
	/**pOptions structure contains members whose values denote different gdrive related options like access level,authorization
	 * and others which are used while initiating the connectionn to gdrive. So we
	 * parse command line options and set the pOptions structure with respective values*
	 * */
    Fudr_Options* pOptions = fudr_options_create(argc, argv);

    /**initialise the connection with gdrive using values in the pOptions structure
     * The gdrive_init function:
     * Initializes the network connection, sets appropriate
	 * settings for the Google Drive session, and ensures the user
	 * has granted necessary access permissions for the Google
	 * Drive account.
	 * This function calls curl_global_init().
	 * **/

    if ( gdrive_init( pOptions->gdrive_access, pOptions->gdrive_auth_file, pOptions->gdrive_cachettl,pOptions->gdrive_interaction_type,
                    pOptions->gdrive_chunk_size, pOptions->gdrive_max_chunks )  )
    {
        fputs("Could not set up a Google Drive connection.\n", stderr);
        return 1;
    }

    /**pass the required poptions members to fuse_main() function call to mount the gdrive files and directories**/
    int returnVal = fuse_main(pOptions->fuse_argc, pOptions->fuse_argv, &fo, (void*) ((pOptions->dir_perms << 9) + pOptions->file_perms));
0

    fudr_options_free(pOptions);
    return returnVal;
}
