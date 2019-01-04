
/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  Copyright (C) 2011       Sebastian Pipping <sebastian@pipping.org>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall amhfs.c `pkg-config fuse --cflags --libs` -o amhfs
*/

#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite()/utimensat() */
#define _XOPEN_SOURCE 700
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

static char* source_dir = NULL;
static char  source_path[256];

void print_source_path(){
	int i = 0;
	while (i < 256){
		printf("%c", source_path[i]);
		i++;
	}
	printf("\n");
}

char amh_encrypt(char in){
  return (char)((in + 3) % 256);
}

char amh_decrypt(char in){
  return (char)((in - 3) % 256);
}

char* prepend_source_dir (char* pre_path, const char* path) {
  strcpy(pre_path, source_dir);
  strcat(pre_path, path);
  return pre_path;
}

static int amh_getattr(const char *path, struct stat *stbuf)
{
	int res;
	
	path = prepend_source_dir(source_path, path);
	res = lstat(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int amh_access(const char *path, int mask)
{
	int res;

	path = prepend_source_dir(source_path, path);
	res = access(path, mask);
	if (res == -1)
		return -errno;

	return 0;
}

static int amh_readlink(const char *path, char *buf, size_t size)
{
	int res;

	path = prepend_source_dir(source_path, path);
	res = readlink(path, buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
}

//amh_readdir- I understand what this does now! Only took me 2 hours
static int amh_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi) 
{
	DIR *dp;
	struct dirent *de;

	(void) offset;
	(void) fi;

	path = prepend_source_dir(source_path, path);
	dp = opendir(path);
	if (dp == NULL)
		return -errno;

	while ((de = readdir(dp)) != NULL) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;
		if (filler(buf, de->d_name, &st, 0))
			break;
	}

	closedir(dp);
	return 0;
}

static int amh_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;

	/* On Linux this could just be 'mknod(path, mode, rdev)' but this
	   is more portable */
	path = prepend_source_dir(source_path, path);
	if (S_ISREG(mode)) {
		res = open(path, O_CREAT | O_EXCL | O_WRONLY, mode);
		if (res >= 0)
			res = close(res);
	} else if (S_ISFIFO(mode))
		res = mkfifo(path, mode);
	else
		res = mknod(path, mode, rdev);
	if (res == -1)
		return -errno;

	return 0;
}

static int amh_mkdir(const char *path, mode_t mode)
{
	int res;

	path = prepend_source_dir(source_path, path);
	res = mkdir(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int amh_unlink(const char *path)
{
	int res;

	path = prepend_source_dir(source_path, path);
	res = unlink(path);
	if (res == -1)
		return -errno;

	return 0;
}

static int amh_rmdir(const char *path)
{
	int res;

	path = prepend_source_dir(source_path, path);
	res = rmdir(path);
	if (res == -1)
		return -errno;

	return 0;
}

static int amh_symlink(const char *from, const char *to)
{
	int res;
	char source_from[256];
	char source_to[256];

	prepend_source_dir(source_from, from);
	prepend_source_dir(source_to,   to  );
	res = symlink(source_from, source_to);
	if (res == -1)
		return -errno;

	return 0;
}

static int amh_rename(const char *from, const char *to)
{
	int res;
	
	char source_from[256];
	char source_to[256];

	prepend_source_dir(source_from, from);
	prepend_source_dir(source_to,   to  );
	res = rename(source_from, source_to);
	if (res == -1)
		return -errno;

	return 0;
}

static int amh_link(const char *from, const char *to)
{
	int res;
	char source_from[256];
	char source_to[256];

	prepend_source_dir(source_from, from);
	prepend_source_dir(source_to,   to  );
	
	int from_len = strlen(&source_from);
	int to_len = strlen(&source_to);
	char src_from[from_len];
	char src_to[to_len];
	strncpy(&src_from, &source_from, from_len);
	strncpy(&src_to, &source_to, to_len);
	
	res = link(source_from, source_to);
	if (res == -1)
		return -errno;

	return 0;
}

static int amh_chmod(const char *path, mode_t mode)
{
	int res;

	path = prepend_source_dir(source_path, path);
	res = chmod(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int amh_chown(const char *path, uid_t uid, gid_t gid)
{
	int res;

	path = prepend_source_dir(source_path, path);
	res = lchown(path, uid, gid);
	if (res == -1)
		return -errno;

	return 0;
}
//Declare these here so it compiles.
static int amh_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi);
static int amh_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi);

static int amh_truncate(const char *path, off_t size)
{
	int res;

	char* buf = malloc(size);

        char *temp = malloc(sizeof(char) * 256);
	strcpy(temp, path);
	//char* temp_source_path = malloc(sizeof(char) * 256);
	//strcpy(temp_source_path, source_path);
	//temp = prepend_source_dir(temp_source_path, path);

	amh_read(temp, buf, size, 0, NULL);
	amh_write(temp, buf, size, 0, NULL);
	
	//res = truncate(path, size);
	//if (res == -1)
	//	return -errno;

	return 0;
}

#ifdef HAVE_UTIMENSAT
static int amh_utimens(const char *path, const struct timespec ts[2])
{
	int res;

	/* don't use utime/utimes since they follow symlinks */
	path = prepend_source_dir(source_path, path);
	res = utimensat(0, path, ts, AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		return -errno;

	return 0;
}
#endif

static int amh_open(const char *path, struct fuse_file_info *fi)
{
	int res;

	path = prepend_source_dir(source_path, path);
	print_source_path();//DEBUG
	printf("Hi\n");
	res = open(path, fi->flags);
	if (res == -1)
		return -errno;

	close(res);

	return 0;
}

static int amh_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
	int fd;
	int res;
	int i;
	char temp_buf[size];

	(void) fi;
	path = prepend_source_dir(source_path, path);
	print_source_path();//debug
	fprintf(stderr,"Hi\n");
	fd = open(path, O_RDONLY);
	if (fd == -1)
		return -errno;

	res = pread(fd, temp_buf, size, offset);
	if (res == -1)
		res = -errno;

	// Move data from temporary buffer into provided one.
	// HERE WOULD BE YOUR CHANGE TO UNCHANGE THE DATA. (See below)
	for (i = 0; i < size; i += 1) {
	  buf[i] = amh_decrypt(temp_buf[i]);
	}

	close(fd);
	return res;
}

static int amh_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
	int fd;
	//int fd2;
	int res;
	//int res2;
	int i;
	char temp_buf[size];

	(void) fi;


	char *temp = malloc(sizeof(char) * 256);
	strcpy(temp, path);
	char* temp_source_path = malloc(sizeof(char) * 256);
	strcpy(temp_source_path, source_path);
	temp = prepend_source_dir(temp_source_path, temp);
	fd = open(temp, O_WRONLY);
	if (fd == -1)
		return -errno;

	// Move data to write into temporary buffer.
	// HERE WOULD BE YOUR CHANCE TO CHANGE THE DATA. (See above)
	for (i = 0; i < size; i += 1) {
	  temp_buf[i] = amh_encrypt(buf[i]);
	}

	res = pwrite(fd, temp_buf, size, offset);
	if (res == -1)
		res = -errno;

	close(fd);
	

	//dumb way to keep track of versions inbound: (abandon hope all ye who enter here)
	int version = 1;
	char version_str[12];
	char* v_path = malloc(sizeof(char) * (strlen(path)+11));//Accomodate for max int size
	char* base_path = malloc(sizeof(char) * (strlen(path)+1));//MEMORY LEAK LOL
	strcpy(base_path, path);//save the original path, without appended version numbers
	sprintf(version_str, "%i", version);
	strcat(path, &version_str);//Turns out, strcat ACTUALLY MODIFIES path.
	strcpy(v_path, path);
	strcpy(path, base_path);//reset changes to path.
	while(amh_access(v_path, 0) == 0){//while a version with this number already exists
	  version++;//increase the version number
	  sprintf(version_str, "%i", version);//stringify the version number
	  strcpy(v_path, base_path);
	  strcat(v_path, &version_str);
	}//until we find a version number with no associated file, ie, the current version to save.
	path = v_path;//update the path.

	path = prepend_source_dir(source_path, path);
	amh_mknod(v_path, 0777, 0);
	//prepend_source_dir(source_path, v_path);
	fd = open(path, O_WRONLY);
	//fd2 = open(v_path, O_WRONLY);
	//if (fd2 == -1) return -errno;
	if (fd == -1)
		return -errno;

	// Move data to write into temporary buffer.
	// HERE WOULD BE YOUR CHANCE TO CHANGE THE DATA. (See above)
	for (i = 0; i < size; i += 1) {
	  temp_buf[i] = amh_encrypt(buf[i]);
	}

	res = pwrite(fd, temp_buf, size, offset);
	//res2 = pwrite(fd2, temp_buf, size, offset);

	if (res == -1)
		res = -errno;

	//if (res2 == -1) res = -errno;

	close(fd);

	//close(fd2);

	//Experimental: Link the base filename to the most recent version
	//base_path = prepend_source_dir(source_path,base_path);
	//unlink(base_path);
	//link(base_path, v_path);
	//amh_unlink(base_path);
	//amh_link(base_path, v_path);
	//free(v_path);
	//free(base_path);//100% perfect memory managment.
	
	return res;
}

static int amh_statfs(const char *path, struct statvfs *stbuf)
{
	int res;

	path = prepend_source_dir(source_path, path);
	res = statvfs(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int amh_release(const char *path, struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void) path;
	(void) fi;
	return 0;
}

static int amh_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void) path;
	(void) isdatasync;
	(void) fi;
	return 0;
}

#ifdef HAVE_POSIX_FALLOCATE
static int amh_fallocate(const char *path, int mode,
			off_t offset, off_t length, struct fuse_file_info *fi)
{
	int fd;
	int res;

	(void) fi;

	if (mode)
		return -EOPNOTSUPP;

	path = prepend_source_dir(source_path, path);
	fd = open(path, O_WRONLY);
	if (fd == -1)
		return -errno;

	res = -posix_fallocate(fd, offset, length);

	close(fd);
	return res;
}
#endif

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int amh_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
{
	path = prepend_source_dir(source_path, path);
	int res = lsetxattr(path, name, value, size, flags);
	if (res == -1)
		return -errno;
	return 0;
}

static int amh_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
	path = prepend_source_dir(source_path, path);
	int res = lgetxattr(path, name, value, size);
	if (res == -1)
		return -errno;
	return res;
}

static int amh_listxattr(const char *path, char *list, size_t size)
{
	path = prepend_source_dir(source_path, path);
	int res = llistxattr(path, list, size);
	if (res == -1)
		return -errno;
	return res;
}

static int amh_removexattr(const char *path, const char *name)
{
	path = prepend_source_dir(source_path, path);
	int res = lremovexattr(path, name);
	if (res == -1)
		return -errno;
	return 0;
}
#endif /* HAVE_SETXATTR */

static struct fuse_operations amh_oper = {
	.getattr	= amh_getattr,
	.access		= amh_access,
	.readlink	= amh_readlink,
	.readdir	= amh_readdir,
	.mknod		= amh_mknod,
	.mkdir		= amh_mkdir,
	.symlink	= amh_symlink,
	.unlink		= amh_unlink,
	.rmdir		= amh_rmdir,
	.rename		= amh_rename,
	.link		= amh_link,
	.chmod		= amh_chmod,
	.chown		= amh_chown,
	.truncate	= amh_truncate,
#ifdef HAVE_UTIMENSAT
	.utimens	= amh_utimens,
#endif
	.open		= amh_open,
	.read		= amh_read,
	.write		= amh_write,
	.statfs		= amh_statfs,
	.release	= amh_release,
	.fsync		= amh_fsync,
#ifdef HAVE_POSIX_FALLOCATE
	.fallocate	= amh_fallocate,
#endif
#ifdef HAVE_SETXATTR
	.setxattr	= amh_setxattr,
	.getxattr	= amh_getxattr,
	.listxattr	= amh_listxattr,
	.removexattr	= amh_removexattr,
#endif
};

int main(int argc, char *argv[])
{
        int i;
	umask(0);
	if (argc < 3) {
	  fprintf(stderr, "USAGE: %s <source directory> <mount point>\n", argv[0]);
	  return 1;
	}
	source_dir = argv[1];
	fprintf(stderr, "DEBUG: Mounting %s at %s\n", source_dir, argv[2]);
	char* short_argv[argc - 1];
	short_argv[0] = argv[0];
	for (i = 2; i < argc; i += 1) {
	  short_argv[i - 1] = argv[i];
	}
	return fuse_main(argc - 1, short_argv, &amh_oper, NULL);
}
