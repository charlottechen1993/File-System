/*
	FUSE: Filesystem in Userspace
	Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
 
	This program can be distributed under the terms of the GNU GPL.
	See the file COPYING.
 */

#define	FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

//size of a disk block
#define	BLOCK_SIZE 512

//we'll use 8.3 filenames
#define	MAX_FILENAME 8
#define	MAX_EXTENSION 3

//How many files can there be in one directory?
#define MAX_FILES_IN_DIR (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + (MAX_EXTENSION + 1) + sizeof(size_t) + sizeof(long))

//The attribute packed means to not align these things
struct cs1550_directory_entry
{
    int nFiles;	//How many files are in this directory.
				//Needs to be less than MAX_FILES_IN_DIR
    
    struct cs1550_file_directory
    {
        char fname[MAX_FILENAME + 1];	//filename (plus space for nul)
        char fext[MAX_EXTENSION + 1];	//extension (plus space for nul)
        size_t fsize;					//file size
        long nStartBlock;				//where the first block is on disk
    } __attribute__((packed)) files[MAX_FILES_IN_DIR];	//There is an array of these
    
    //This is some space to get this to be exactly the size of the disk block.
    //Don't use it for anything.
    char padding[BLOCK_SIZE - MAX_FILES_IN_DIR * sizeof(struct cs1550_file_directory) - sizeof(int)];
} ;

typedef struct cs1550_root_directory cs1550_root_directory;

#define MAX_DIRS_IN_ROOT (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + sizeof(long))

struct cs1550_root_directory
{
    int nDirectories;	//How many subdirectories are in the root
    //Needs to be less than MAX_DIRS_IN_ROOT
    struct cs1550_directory
    {
        char dname[MAX_FILENAME + 1];	//directory name (plus space for nul)
        long nStartBlock;				//where the directory block is on disk
    } __attribute__((packed)) directories[MAX_DIRS_IN_ROOT];	//There is an array of these
    
    //This is some space to get this to be exactly the size of the disk block.
    //Don't use it for anything.
    char padding[BLOCK_SIZE - MAX_DIRS_IN_ROOT * sizeof(struct cs1550_directory) - sizeof(int)];
} ;


typedef struct cs1550_directory_entry cs1550_directory_entry;

//How much data can one block hold?
#define	MAX_DATA_IN_BLOCK (BLOCK_SIZE - sizeof(long))

struct cs1550_disk_block
{
    //The next disk block, if needed. This is the next pointer in the linked
    //allocation list
    long nNextBlock;
    
    //And all the rest of the space in the block can be used for actual data
    //storage.
    char data[MAX_DATA_IN_BLOCK];
};


typedef struct cs1550_disk_block cs1550_disk_block;

// ============================================================================
// ============================= cs1550_getattr() =============================
// ============================================================================
/*
 * Called whenever the system wants to know the file attributes, including
 * simply whether the file exists or not.
 *
 * man -s 2 stat will show the fields of a stat structure
 */
static int cs1550_getattr(const char *path, struct stat *stbuf)
{
    int res = 0;
    int count = 0;
    char directory_name[MAX_FILENAME + 1];		// subdirectory
    char filename[MAX_FILENAME + 1];			// filename
    char extension[MAX_EXTENSION + 1];			// extension
    
    printf("GETTTTTATTTTR\n");
    memset(stbuf, 0, sizeof(struct stat));
    
    /******************
     * If path is root
     ******************/
    if (strcmp(path, "/") == 0) {
        printf("here: root");
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    }
    //If path isn't the root
    else {
        printf("here: not root\n");
        count = sscanf(path, "/%[^/]/%[^.].%s", directory_name, filename, extension);
        printf("directory_name %s\n", directory_name);
        printf("filename %s\n", filename);
        printf("extension %s\n", extension);
        
        memset(directory_name, 0, (MAX_FILENAME + 1));
        memset(filename, 0, (MAX_FILENAME + 1));
        memset(extension, 0, (MAX_EXTENSION + 1));
        
        FILE *file = fopen(".disk", "rb");	// open .disk
        
        cs1550_root_directory * root_dir = malloc(sizeof(cs1550_root_directory));
        cs1550_directory_entry * dir_entry = malloc(sizeof(cs1550_directory_entry));
        // cs1550_root_directory root_dir;
        // cs1550_directory_entry dir_entry;
        
        //Seek to the position of the root directory
        fseek(file, 0, SEEK_SET);
        //Read into memory
        fread(root_dir, sizeof(cs1550_root_directory), 1, file);
        
        //Loop through the array of directories in root for "directory_name"
        int i = 0;
        for(i=0; i < MAX_DIRS_IN_ROOT; i++){
            res = -ENOENT;
            
            //If "directory_name" exists as a subdirectory
            if(strcmp(root_dir->directories[i].dname, directory_name)==0){
                int cur = 0;
                cur = root_dir->directories[i].nStartBlock; //position of cur subdirectory
                /************************
                 * If path is a directory
                 ************************/
                if(count == 1){
                    //Might want to return a structure with these fields
                    stbuf->st_mode = S_IFDIR | 0755;
                    stbuf->st_nlink = 2;
                    res = 0; //no error
                }
                /*******************
                 * If path is a file
                 *******************/
                else if (count > 1){
                    size_t file_size = -1;
                    int found_file = 0;
                    
                    fseek(file, cur, SEEK_SET);									// seek to correct position of the subdirectory
                    fread(dir_entry, sizeof(cs1550_directory_entry), 1, file);	// get block
                    
                    //Loop through all files in cur sub-directory for "filename"
                    int i = 0;
                    for(i=0; i<MAX_FILES_IN_DIR; i++){
                        //if find file in current sub-directory that matches filename in path
                        if(strcmp(dir_entry->files[i].fname, filename)==0 && strcmp(dir_entry->files[i].fext, filename)==0){
                            file_size = dir_entry->files[i].fsize;
                            found_file = 1;
                            break;
                        }
                    }
                    //if file exist, return permission and size; else return -ENOENT
                    if(found_file==1){
                        stbuf->st_mode = S_IFREG | 0666;
                        stbuf->st_nlink = 1; 				//file links
                        stbuf->st_size = file_size; 		//file size
                        res = 0; // no error
                    } else {
                        res = -ENOENT;
                    }
                }
                //break out of loop if found subdirectory (or file)
                break;
            }
        }
        //free up mem space allocated for structs
        free(root_dir);
        free(dir_entry);
        
        //res = -ENOENT if subdirectory doesn't exist after looping through directories in root
        
        //close disk file
        fclose(file);
    }
    return res;
}

// ============================================================================
// ============================= cs1550_readdir() =============================
// ============================================================================
/*
 * Called whenever the contents of a directory are desired. Could be from an 'ls'
 * or could even be when a user hits TAB to do autocompletion
 */
static int cs1550_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                          off_t offset, struct fuse_file_info *fi)
{
    //Since we're building with -Wall (all warnings reported) we need
    //to "use" every parameter, so let's just cast them to void to
    //satisfy the compiler
    (void) offset;
    (void) fi;
    char directory_name[MAX_FILENAME + 1];		// subdirectory
    char filename[MAX_FILENAME + 1];			// filename from path
    char extension[MAX_EXTENSION + 1];			// extension
    
    memset(directory_name, 0, (MAX_FILENAME + 1));   	// initialize directory to 0
    memset(filename, 0, (MAX_FILENAME + 1));   	// initialize filename to 0
    memset(extension, 0, (MAX_EXTENSION + 1));		// initialize extension to 0
    int res = 0;
    
    FILE * file = fopen(".disk", "rb");
    cs1550_root_directory * root_dir = malloc(sizeof(cs1550_root_directory));
    cs1550_directory_entry * dir_entry = malloc(sizeof(cs1550_directory_entry));
    
    //If path is not root
    if (strcmp(path, "/") != 0){
        sscanf(path, "/%[^/]/%[^.].%s", directory_name, filename, extension);
        
        int i=0;
        for(i=0; i < MAX_DIRS_IN_ROOT; i++){
            res = -ENOENT;
            
            //If "directory_name" exists as a subdirectory
            if(strcmp(root_dir->directories[i].dname, directory_name)==0){
                int cur = 0;
                cur = root_dir->directories[i].nStartBlock; //position of cur subdirectory
                
                fseek(file, sizeof(cs1550_root_directory)*cur , SEEK_SET);
                fread(dir_entry, sizeof(cs1550_root_directory), 1, file);
                filler(buf, ".", NULL,0);
                filler(buf, "..", NULL, 0);
                
                int j = 0;
                for(j=0; j<MAX_FILES_IN_DIR; j++){
                    if(dir_entry->files[i].fname[0] == '\0'){
                        continue;
                    } else if (strlen(dir_entry->files[i].fext)>0){
                        char fullname[12];
                        strcat(fullname, dir_entry->files[i].fname);
                        filler(buf, fullname, NULL, 0);
                    } else {
                        char fullname[12];
                        strcat(fullname, dir_entry->files[i].fname);
                        strcat(fullname, ".");
                        strcat(fullname, dir_entry->files[i].fext);
                        filler(buf, fullname, NULL, 0);
                    }
                }
                res=0;
                break;
            }
        }
    }
    //If path is root
    else {
        fseek(file, 0, SEEK_SET);
        fread(root_dir, sizeof(cs1550_root_directory), 1, file);
        
        //the filler function allows us to add entries to the listing
        //read the fuse.h file for a description (in the ../include dir)
        filler(buf, ".", NULL,0);
        filler(buf, "..", NULL, 0);
        
        //print all directories in root directory
        int i=0;
        for(i=0; i<root_dir->nDirectories; i++){
            filler(buf, root_dir->directories[i].dname, NULL, 0);
        }
        res = 0;
    }
    fclose(file);
    /*
     //add the user stuff (subdirs or files)
     //the +1 skips the leading '/' on the filenames
     filler(buf, newpath + 1, NULL, 0);
     */
    return res;
}

// ============================================================================
// ============================== cs1550_mkdir() ==============================
// ============================================================================
/*
 * Creates a directory. We can ignore mode since we're not dealing with
 * permissions, as long as getattr returns appropriate ones for us.
 */
static int cs1550_mkdir(const char *path, mode_t mode)
{
    (void) path;
    (void) mode;
    char directory_name[MAX_FILENAME + 1];		// subdirectory
    char filename[MAX_FILENAME + 1];			// filename
    char extension[MAX_EXTENSION + 1];			// extension
    
    int count = 0;
    count = sscanf(path, "/%[^/]/%[^.].%s", directory_name, filename, extension);
    
    memset(directory_name, 0, (MAX_FILENAME + 1));
   	memset(filename, 0, (MAX_FILENAME + 1));
   	memset(extension, 0, (MAX_EXTENSION + 1));
   	
    //check for errors
    if(strlen(directory_name)>MAX_FILENAME){
        return -ENAMETOOLONG;
    } else if (strlen(filename)!=0){
        return -EPERM;
    } else {
        cs1550_root_directory * root_dir = malloc(sizeof(cs1550_root_directory));	// root
        cs1550_directory_entry * new_dir = malloc(sizeof(cs1550_directory_entry));	// new directory
        memset(new_dir, 0, (MAX_FILENAME + 1));
        
        FILE * file = fopen(".disk", "rb+");	// open .disk
        //Seek to the position of the root directory
        fseek(file, 0, SEEK_SET);
        //Read into memory
        fread(root_dir, sizeof(cs1550_root_directory), 1, file);
        
        //check if directory exists
        int i = 0;
        int dir_exist = -1;
        for(i=0; i<MAX_DIRS_IN_ROOT; i++){
            if(strcmp(root_dir->directories[i].dname, directory_name)==0){
                dir_exist = 1;
                break;
            }
        }
        if(dir_exist==-1){
            return -EEXIST;
        } else {
            char bitmap[10240]=""; //bitmap array represents 10240 block
            int bitmap_pos = 20*512; //space the bitmap take up on disk
            int newdir_pos = -1;
            
            //load bitmap from disk
            fseek(file, -bitmap_pos, SEEK_END);
            fread(bitmap, sizeof(char)*10240, 1, file);
            
            //look for free blocks for new directory
            int i = 513;
            for(i = 513; i<sizeof(bitmap); i++){
                if(bitmap[i]==0){
                    bitmap[i] = 1;
                    newdir_pos = i;
                    /*--------------
                     * Update Bitmap
                     ---------------*/
                    fseek(file, -bitmap_pos, SEEK_END);
                    fwrite(bitmap, sizeof(bitmap), 1, file);
                    break;
                }
            }
            /*------------
             * Update root
             -------------*/
            fseek(file, 0, SEEK_SET);
            fread(root_dir, sizeof(cs1550_root_directory), 1, file);
            
            if(root_dir->nDirectories == NULL){
                root_dir->nDirectories = 0;
            }
            root_dir->nDirectories += 1; //increment number of directories in root
            
            //set name and position of new directory
            strcpy(root_dir->directories[(root_dir->nDirectories)-1].dname, directory_name);
            root_dir->directories[(root_dir->nDirectories)-1].nStartBlock = newdir_pos;
            
            fseek(file, 0, SEEK_SET);
            fwrite(root_dir, sizeof(cs1550_root_directory),1,file);
            
            /*-------------------------
             * Initialize new directory
             ------------------------*/
            new_dir->nFiles = 0;
            fseek(file, sizeof(cs1550_directory_entry)*newdir_pos, SEEK_SET);
            fwrite(new_dir, sizeof(cs1550_directory_entry),1,file);
        }
        fclose(file);
    }
    return 0;
}

/*
 * Removes a directory.
 */
static int cs1550_rmdir(const char *path)
{
    (void) path;
    return 0;
}

/******************************************************************************
 *
 *                                   PART 2
 *
 *****************************************************************************/

// ============================================================================
// ============================== cs1550_mknod() ==============================
// ============================================================================
/*
 * Does the actual creation of a file. Mode and dev can be ignored.
 *
 */
static int cs1550_mknod(const char *path, mode_t mode, dev_t dev)
{
    (void) mode;
    (void) dev;
    return 0;
}

// ============================================================================
// ============================== cs1550_unlink() =============================
// ============================================================================
/*
 * Deletes a file
 */
static int cs1550_unlink(const char *path)
{
    (void) path;
    
    return 0;
}

// ============================================================================
// ============================== cs1550_read() ===============================
// ============================================================================
/*
 * Read size bytes from file into buf starting from offset
 *
 */
static int cs1550_read(const char *path, char *buf, size_t size, off_t offset,
                       struct fuse_file_info *fi)
{
    (void) buf;
    (void) offset;
    (void) fi;
    (void) path;
    
    //check to make sure path exists
    //check that size is > 0
    //check that offset is <= to the file size
    //read in data
    //set size and return, or error
    
    size = 0;
    
    return size;
}

// ============================================================================
// ============================== cs1550_write() ==============================
// ============================================================================
/* 
 * Write size bytes from buf into file starting from offset
 *
 */
static int cs1550_write(const char *path, const char *buf, size_t size, 
                        off_t offset, struct fuse_file_info *fi)
{
    (void) buf;
    (void) offset;
    (void) fi;
    (void) path;
    
    //check to make sure path exists
    //check that size is > 0
    //check that offset is <= to the file size
    //write data
    //set size (should be same as input) and return, or error
    
    return size;
}

/******************************************************************************
 *
 *  DO NOT MODIFY ANYTHING BELOW THIS LINE
 *
 *****************************************************************************/

/*
 * truncate is called when a new file is created (with a 0 size) or when an
 * existing file is made shorter. We're not handling deleting files or 
 * truncating existing ones, so all we need to do here is to initialize
 * the appropriate directory entry.
 *
 */
static int cs1550_truncate(const char *path, off_t size)
{
    (void) path;
    (void) size;
    
    return 0;
}


/* 
 * Called when we open a file
 *
 */
static int cs1550_open(const char *path, struct fuse_file_info *fi)
{
    (void) path;
    (void) fi;
    /*
     //if we can't find the desired file, return an error
     return -ENOENT;
     */
    
    //It's not really necessary for this project to anything in open
    
    /* We're not going to worry about permissions for this project, but 
     if we were and we don't have them to the file we should return an error
     
     return -EACCES;
     */
    
    return 0; //success!
}

/*
 * Called when close is called on a file descriptor, but because it might
 * have been dup'ed, this isn't a guarantee we won't ever need the file 
 * again. For us, return success simply to avoid the unimplemented error
 * in the debug log.
 */
static int cs1550_flush (const char *path , struct fuse_file_info *fi)
{
    (void) path;
    (void) fi;
    
    return 0; //success!
}


//register our new functions as the implementations of the syscalls
static struct fuse_operations hello_oper = {
    .getattr	= cs1550_getattr,
    .readdir	= cs1550_readdir,
    .mkdir	= cs1550_mkdir,
    .rmdir = cs1550_rmdir,
    .read	= cs1550_read,
    .write	= cs1550_write,
    .mknod	= cs1550_mknod,
    .unlink = cs1550_unlink,
    .truncate = cs1550_truncate,
    .flush = cs1550_flush,
    .open	= cs1550_open,
};

//Don't change this.
int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &hello_oper, NULL);
}
