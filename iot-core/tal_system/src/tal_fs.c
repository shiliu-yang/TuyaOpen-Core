/**
 * @file tal_fs.c
 * @brief tal_fs platform dispatch: forwards the 18 interfaces to the port's
 *        file system implementation (tal_fs_ops_platform).
 *
 * iot-core holds no file system logic -- medium, path mapping and handle
 * shape all live in the port (see the tal_fs_ops.h contract). Dispatch goes
 * through TAL_FS_EXEC_OP, which checks the card and the member before calling
 * them and returns the given default otherwise.
 *
 * Within this repository the only caller is p2p-core/local_store; when no
 * tal_fs symbol is referenced the static library does not pull in tal_fs.o
 * and the port's tal_fs_ops_platform need not exist. The platform
 * implementation (fs_card.c) is supplied by the application build.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 */
#include "tal_fs.h"
#include "tal_fs_ops.h"
#include "tal_log.h"

/* Platform ops card (defined in the platform's fs_card.c), dispatched by core
 * through the getter */
extern TAL_FS_OPS_T tal_fs_ops_platform;

TAL_FS_OPS_T *tal_fs_get_ops(void)
{
    return &tal_fs_ops_platform;
}

#define TAL_FS_EXEC_OP(op_name, default_ret, ...)                                                                      \
    do {                                                                                                               \
        TAL_FS_OPS_T *ops = tal_fs_get_ops();                                                                          \
        if (NULL != ops && NULL != ops->op_name) {                                                                     \
            return ops->op_name(__VA_ARGS__);                                                                          \
        }                                                                                                              \
        PR_ERR("TAL FS operation %s not available", #op_name);                                                         \
        return default_ret;                                                                                            \
    } while (0)

/* ---------------------------------------------------------------------------
 * File
 * --------------------------------------------------------------------------- */
TUYA_FILE tal_fopen(const char *path, const char *mode)
{
    TAL_FS_EXEC_OP(fopen, NULL, path, mode);
}

int tal_fclose(TUYA_FILE file)
{
    TAL_FS_EXEC_OP(fclose, -1, file);
}

int tal_fwrite(void *buf, int bytes, TUYA_FILE file)
{
    TAL_FS_EXEC_OP(fwrite, -1, buf, bytes, file);
}

int tal_fread(void *buf, int bytes, TUYA_FILE file)
{
    TAL_FS_EXEC_OP(fread, -1, buf, bytes, file);
}

int tal_fseek(TUYA_FILE file, int64_t offs, int whence)
{
    TAL_FS_EXEC_OP(fseek, -1, file, offs, whence);
}

int tal_fflush(TUYA_FILE file)
{
    TAL_FS_EXEC_OP(fflush, -1, file);
}

int tal_fsync(int fd)
{
    TAL_FS_EXEC_OP(fsync, -1, fd);
}

int tal_fileno(TUYA_FILE file)
{
    TAL_FS_EXEC_OP(fileno, -1, file);
}

char *tal_fgets(char *buf, int len, TUYA_FILE file)
{
    TAL_FS_EXEC_OP(fgets, NULL, buf, len, file);
}

/* ---------------------------------------------------------------------------
 * Path
 * --------------------------------------------------------------------------- */
int tal_fs_is_exist(const char *path, BOOL_T *is_exist)
{
    TAL_FS_EXEC_OP(fs_is_exist, -1, path, is_exist);
}

int tal_fs_mkdir(const char *path)
{
    TAL_FS_EXEC_OP(fs_mkdir, -1, path);
}

int tal_fs_remove(const char *path)
{
    TAL_FS_EXEC_OP(fs_remove, -1, path);
}

int tal_fs_rename(const char *path_old, const char *path_new)
{
    TAL_FS_EXEC_OP(fs_rename, -1, path_old, path_new);
}

/* ---------------------------------------------------------------------------
 * Directory
 * --------------------------------------------------------------------------- */
int tal_dir_open(const char *path, TUYA_DIR *dir)
{
    TAL_FS_EXEC_OP(dir_open, -1, path, dir);
}

int tal_dir_close(TUYA_DIR dir)
{
    TAL_FS_EXEC_OP(dir_close, -1, dir);
}

int tal_dir_read(TUYA_DIR dir, TUYA_FILEINFO *info)
{
    TAL_FS_EXEC_OP(dir_read, -1, dir, info);
}

int tal_dir_name(TUYA_FILEINFO info, const char **name)
{
    TAL_FS_EXEC_OP(dir_name, -1, info, name);
}

int tal_dir_is_directory(TUYA_FILEINFO info, BOOL_T *is_dir)
{
    TAL_FS_EXEC_OP(dir_is_directory, -1, info, is_dir);
}
