/******************************************************************************
 * ventoy.c 
 *
 * Copyright (c) 2020, longpanda <admin@ventoy.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/disk.h>
#include <grub/device.h>
#include <grub/term.h>
#include <grub/partition.h>
#include <grub/file.h>
#include <grub/normal.h>
#include <grub/extcmd.h>
#include <grub/datetime.h>
#include <grub/i18n.h>
#include <grub/net.h>
#include <grub/misc.h>
#include <grub/kernel.h>
#ifdef GRUB_MACHINE_EFI
#include <grub/efi/efi.h>
#endif
#include <grub/time.h>
#include <grub/relocator.h>
#include <grub/ventoy.h>
#include "ventoy_def.h"

GRUB_MOD_LICENSE ("GPLv3+");

int g_ventoy_debug = 0;
static int g_efi_os = 0xFF;
initrd_info *g_initrd_img_list = NULL;
initrd_info *g_initrd_img_tail = NULL;
int g_initrd_img_count = 0;
int g_valid_initrd_count = 0;
int g_default_menu_mode = 0;
int g_filt_dot_underscore_file = 0;
static grub_file_t g_old_file;
static int g_ventoy_last_entry_back;

char g_iso_path[256];
char g_img_swap_tmp_buf[1024];
img_info g_img_swap_tmp;
img_info *g_ventoy_img_list = NULL;

int g_ventoy_img_count = 0;

grub_device_t g_enum_dev = NULL;
grub_fs_t g_enum_fs = NULL;
img_iterator_node g_img_iterator_head;
img_iterator_node *g_img_iterator_tail = NULL;

grub_uint8_t g_ventoy_break_level = 0;
grub_uint8_t g_ventoy_debug_level = 0;
grub_uint8_t g_ventoy_chain_type = 0;

grub_uint8_t *g_ventoy_cpio_buf = NULL;
grub_uint32_t g_ventoy_cpio_size = 0;
cpio_newc_header *g_ventoy_initrd_head = NULL;
grub_uint8_t *g_ventoy_runtime_buf = NULL;

ventoy_grub_param *g_grub_param = NULL;

ventoy_guid  g_ventoy_guid = VENTOY_GUID;

ventoy_img_chunk_list g_img_chunk_list;

int g_wimboot_enable = 0;
ventoy_img_chunk_list g_wimiso_chunk_list;
char *g_wimiso_path = NULL;

static char *g_tree_script_buf = NULL;
static int g_tree_script_pos = 0;

static char *g_list_script_buf = NULL;
static int g_list_script_pos = 0;

static char *g_part_list_buf = NULL;
static int g_part_list_pos = 0;

static const char *g_menu_class[] = 
{
    "vtoyiso", "vtoywim", "vtoyefi", "vtoyimg"
};
    
static const char *g_menu_prefix[] = 
{
    "iso", "wim", "efi", "img"
};

void ventoy_debug(const char *fmt, ...)
{
    va_list args;

    va_start (args, fmt);
    grub_vprintf (fmt, args);
    va_end (args);
}

int ventoy_is_efi_os(void)
{
    if (g_efi_os > 1)
    {
        g_efi_os = (grub_strstr(GRUB_PLATFORM, "efi")) ? 1 : 0;
    }

    return g_efi_os;
}

static int ventoy_get_fs_type(const char *fs)
{
    if (NULL == fs)
    {
        return ventoy_fs_max;
    }
    else if (grub_strncmp(fs, "exfat", 5) == 0)
    {
        return ventoy_fs_exfat;
    }
    else if (grub_strncmp(fs, "ntfs", 4) == 0)
    {
        return ventoy_fs_ntfs;
    }
    else if (grub_strncmp(fs, "ext", 3) == 0)
    {
        return ventoy_fs_ext;
    }
    else if (grub_strncmp(fs, "xfs", 3) == 0)
    {
        return ventoy_fs_xfs;
    }
    else if (grub_strncmp(fs, "udf", 3) == 0)
    {
        return ventoy_fs_udf;
    }
    else if (grub_strncmp(fs, "fat", 3) == 0)
    {
        return ventoy_fs_fat;
    }

    return ventoy_fs_max;
}

static int ventoy_string_check(const char *str, grub_char_check_func check)
{
    if (!str)
    {
        return 0;
    }
    
    for ( ; *str; str++)
    {
        if (!check(*str))
        {
            return 0;
        }
    }

    return 1;
}


static grub_ssize_t ventoy_fs_read(grub_file_t file, char *buf, grub_size_t len)
{
    grub_memcpy(buf, (char *)file->data + file->offset, len);
    return len;
}

static grub_err_t ventoy_fs_close(grub_file_t file)
{
    grub_file_close(g_old_file);
    grub_free(file->data);

    file->device = 0;
    file->name = 0;

    return 0;
}

static grub_file_t ventoy_wrapper_open(grub_file_t rawFile, enum grub_file_type type)
{
    int len;
    grub_file_t file;
    static struct grub_fs vtoy_fs =
    {
        .name = "vtoy",
        .fs_dir = 0,
        .fs_open = 0,
        .fs_read = ventoy_fs_read,
        .fs_close = ventoy_fs_close,
        .fs_label = 0,
        .next = 0
    };

    if (type != 52)
    {
        return rawFile;
    }

    file = (grub_file_t)grub_zalloc(sizeof (*file));
    if (!file)
    {
        return 0;
    }

    file->data = grub_malloc(rawFile->size + 4096);
    if (!file->data)
    {
        return 0;
    }

    grub_file_read(rawFile, file->data, rawFile->size);
    len = ventoy_fill_data(4096, (char *)file->data + rawFile->size);

    g_old_file = rawFile;
    
    file->size = rawFile->size + len;
    file->device = rawFile->device;
    file->fs = &vtoy_fs;
    file->not_easily_seekable = 1;

    return file;
}

static int ventoy_check_decimal_var(const char *name, long *value)
{
    const char *value_str = NULL;
    
    value_str = grub_env_get(name);
    if (NULL == value_str)
    {
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "Variable %s not found", name);
    }

    if (!ventoy_is_decimal(value_str))
    {
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "Variable %s value '%s' is not an integer", name, value_str);
    }

    *value = grub_strtol(value_str, NULL, 10);

    return GRUB_ERR_NONE;
}

static grub_err_t ventoy_cmd_debug(grub_extcmd_context_t ctxt, int argc, char **args)
{
    if (argc != 1)
    {
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "Usage: %s {on|off}", cmd_raw_name);
    }

    if (0 == grub_strcmp(args[0], "on"))
    {
        g_ventoy_debug = 1;
        grub_env_set("vtdebug_flag", "debug");
    }
    else
    {
        g_ventoy_debug = 0;
        grub_env_set("vtdebug_flag", "");
    }

    VENTOY_CMD_RETURN(GRUB_ERR_NONE);
}

static grub_err_t ventoy_cmd_break(grub_extcmd_context_t ctxt, int argc, char **args)
{
    (void)ctxt;

    if (argc < 1 || (args[0][0] != '0' && args[0][0] != '1'))
    {
        grub_printf("Usage: %s {level} [debug]\r\n", cmd_raw_name);
        grub_printf(" level:\r\n");
        grub_printf("    01/11: busybox / (+cat log)\r\n");
        grub_printf("    02/12: initrd / (+cat log)\r\n");
        grub_printf("    03/13: hook / (+cat log)\r\n");
        grub_printf("\r\n");
        grub_printf(" debug:\r\n");
        grub_printf("    0: debug is on\r\n");
        grub_printf("    1: debug is off\r\n");
        grub_printf("\r\n");
        VENTOY_CMD_RETURN(GRUB_ERR_NONE);
    }

    g_ventoy_break_level = (grub_uint8_t)grub_strtoul(args[0], NULL, 16);

    if (argc > 1 && grub_strtoul(args[1], NULL, 10) > 0)
    {
        g_ventoy_debug_level = 1;
    }

    VENTOY_CMD_RETURN(GRUB_ERR_NONE);
}

static grub_err_t ventoy_cmd_strstr(grub_extcmd_context_t ctxt, int argc, char **args)
{
    (void)ctxt;

    if (argc != 2)
    {
        return 1;
    }

    return (grub_strstr(args[0], args[1])) ? 0 : 1;
}

static grub_err_t ventoy_cmd_strbegin(grub_extcmd_context_t ctxt, int argc, char **args)
{
    char *c0, *c1;
    
    (void)ctxt;

    if (argc != 2)
    {
        return 1;
    }

    c0 = args[0];
    c1 = args[1];

    while (*c0 && *c1)
    {
        if (*c0 != *c1)
        {
            return 1;
        }
        c0++;
        c1++;
    }

    if (*c1)
    {
        return 1;
    }

    return 0;
}

static grub_err_t ventoy_cmd_incr(grub_extcmd_context_t ctxt, int argc, char **args)
{
    long value_long = 0;
    char buf[32];
    
    if ((argc != 2) || (!ventoy_is_decimal(args[1])))
    {
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "Usage: %s {Variable} {Int}", cmd_raw_name);
    }

    if (GRUB_ERR_NONE != ventoy_check_decimal_var(args[0], &value_long))
    {
        return grub_errno;
    }

    value_long += grub_strtol(args[1], NULL, 10);

    grub_snprintf(buf, sizeof(buf), "%ld", value_long);
    grub_env_set(args[0], buf);

    VENTOY_CMD_RETURN(GRUB_ERR_NONE);
}

static grub_err_t ventoy_cmd_file_size(grub_extcmd_context_t ctxt, int argc, char **args)
{
    int rc = 1;
    char buf[32];
    grub_file_t file;
    
    (void)ctxt;
    (void)argc;
    (void)args;

    if (argc != 2)
    {
        return rc;
    }

    file = ventoy_grub_file_open(VENTOY_FILE_TYPE, "%s", args[0]);
    if (file == NULL)
    {
        debug("failed to open file <%s> for udf check\n", args[0]);
        return 1;
    }

    grub_snprintf(buf, sizeof(buf), "%llu", (unsigned long long)file->size);

    grub_env_set(args[1], buf);

    grub_file_close(file); 
    rc = 0;
    
    return rc;
}

static grub_err_t ventoy_cmd_load_wimboot(grub_extcmd_context_t ctxt, int argc, char **args)
{
    grub_file_t file;
    
    (void)ctxt;
    (void)argc;
    (void)args;

    g_wimboot_enable = 0;
    grub_check_free(g_wimiso_path);
    grub_check_free(g_wimiso_chunk_list.chunk);

    file = grub_file_open(args[0], VENTOY_FILE_TYPE);
    if (!file)
    {
        return 0;
    }

    grub_memset(&g_wimiso_chunk_list, 0, sizeof(g_wimiso_chunk_list));
    g_wimiso_chunk_list.chunk = grub_malloc(sizeof(ventoy_img_chunk) * DEFAULT_CHUNK_NUM);
    if (NULL == g_wimiso_chunk_list.chunk)
    {
        return grub_error(GRUB_ERR_OUT_OF_MEMORY, "Can't allocate image chunk memoty\n");
    }
    
    g_wimiso_chunk_list.max_chunk = DEFAULT_CHUNK_NUM;
    g_wimiso_chunk_list.cur_chunk = 0;

    ventoy_get_block_list(file, &g_wimiso_chunk_list, file->device->disk->partition->start);

    g_wimboot_enable = 1;
    g_wimiso_path = grub_strdup(args[0]);
    
    grub_file_close(file);

    return 0;
}

static int ventoy_load_efiboot_template(char **buf, int *datalen, int *direntoff)
{
    int len;
    grub_file_t file;
    char exec[128];
    char *data = NULL;
    grub_uint32_t offset;

    file = ventoy_grub_file_open(GRUB_FILE_TYPE_LINUX_INITRD, "%s/ventoy/ventoy_efiboot.img.xz", ventoy_get_env("vtoy_efi_part"));
    if (file == NULL)
    {
        debug("failed to open file <%s>\n", "ventoy_efiboot.img.xz");
        return 1;
    }

    len = (int)file->size;
    
    data = (char *)grub_malloc(file->size);
    if (!data)
    {
        return 1;
    }
    
    grub_file_read(file, data, file->size);
    grub_file_close(file); 

    grub_snprintf(exec, sizeof(exec), "loopback efiboot mem:0x%llx:size:%d", (ulonglong)(ulong)data, len);
    grub_script_execute_sourcecode(exec);

    file = grub_file_open("(efiboot)/EFI/BOOT/BOOTX64.EFI", GRUB_FILE_TYPE_LINUX_INITRD);    
    offset = (grub_uint32_t)grub_iso9660_get_last_file_dirent_pos(file);
    grub_file_close(file);
    
    grub_script_execute_sourcecode("loopback -d efiboot");

    *buf = data;
    *datalen = len;
    *direntoff = offset + 2;

    return 0;
}

static grub_err_t ventoy_cmd_concat_efi_iso(grub_extcmd_context_t ctxt, int argc, char **args)
{
    int len = 0;
    int totlen = 0;
    int offset = 0;
    grub_file_t file;
    char name[32];
    char value[32];
    char *buf = NULL;
    char *data = NULL;
    ventoy_iso9660_override *dirent;
    
    (void)ctxt;

    if (argc != 2)
    {
        return 1;
    }

    totlen = sizeof(ventoy_chain_head);

    if (ventoy_load_efiboot_template(&buf, &len, &offset))
    {
        debug("failed to load efiboot template %d\n", len);
        return 1;
    }

    totlen += len;
    
    debug("efiboot template len:%d offset:%d\n", len, offset);

    file = ventoy_grub_file_open(GRUB_FILE_TYPE_LINUX_INITRD, "%s", args[0]);
    if (file == NULL)
    {
        debug("failed to open file <%s>\n", args[0]);
        return 1;
    }

    totlen += ventoy_align_2k(file->size);

    dirent = (ventoy_iso9660_override *)(buf + offset);
    dirent->first_sector = len / 2048;
    dirent->first_sector_be = grub_swap_bytes32(dirent->first_sector);
    dirent->size = (grub_uint32_t)file->size;
    dirent->size_be = grub_swap_bytes32(dirent->size);

    debug("rawiso len:%d efilen:%d total:%d\n", len, (int)file->size, totlen);

#ifdef GRUB_MACHINE_EFI
    data = (char *)grub_efi_allocate_iso_buf(totlen);
#else
    data = (char *)grub_malloc(totlen);
#endif   

    ventoy_fill_os_param(file, (ventoy_os_param *)data);

    grub_memcpy(data + sizeof(ventoy_chain_head), buf, len);
    grub_check_free(buf);

    grub_file_read(file, data + sizeof(ventoy_chain_head) + len, file->size);
    grub_file_close(file); 

    grub_snprintf(name, sizeof(name), "%s_addr", args[1]);
    grub_snprintf(value, sizeof(value), "0x%llx", (ulonglong)(ulong)data);
    grub_env_set(name, value);
    
    grub_snprintf(name, sizeof(name), "%s_size", args[1]);
    grub_snprintf(value, sizeof(value), "%d", (int)(totlen));
    grub_env_set(name, value);

    return 0;
}

static grub_err_t ventoy_cmd_load_file_to_mem(grub_extcmd_context_t ctxt, int argc, char **args)
{
    int rc = 1;
    char name[32];
    char value[32];
    char *buf = NULL;
    grub_file_t file;
    
    (void)ctxt;
    (void)argc;
    (void)args;

    if (argc != 2)
    {
        return rc;
    }

    file = ventoy_grub_file_open(VENTOY_FILE_TYPE, "%s", args[0]);
    if (file == NULL)
    {
        debug("failed to open file <%s>\n", args[0]);
        return 1;
    }

#ifdef GRUB_MACHINE_EFI
    buf = (char *)grub_efi_allocate_iso_buf(file->size);
#else
    buf = (char *)grub_malloc(file->size);
#endif   

    grub_file_read(file, buf, file->size);

    grub_snprintf(name, sizeof(name), "%s_addr", args[1]);
    grub_snprintf(value, sizeof(value), "0x%llx", (unsigned long long)(unsigned long)buf);
    grub_env_set(name, value);
    
    grub_snprintf(name, sizeof(name), "%s_size", args[1]);
    grub_snprintf(value, sizeof(value), "%llu", (unsigned long long)file->size);
    grub_env_set(name, value);

    grub_file_close(file); 
    rc = 0;
    
    return rc;
}

static grub_err_t ventoy_cmd_load_img_memdisk(grub_extcmd_context_t ctxt, int argc, char **args)
{
    int rc = 1;
    int headlen;
    char name[32];
    char value[32];
    char *buf = NULL;
    grub_file_t file;
    
    (void)ctxt;
    (void)argc;
    (void)args;

    if (argc != 2)
    {
        return rc;
    }

    file = ventoy_grub_file_open(VENTOY_FILE_TYPE, "%s", args[0]);
    if (file == NULL)
    {
        debug("failed to open file <%s> for udf check\n", args[0]);
        return 1;
    }

    headlen = sizeof(ventoy_chain_head);

#ifdef GRUB_MACHINE_EFI
    buf = (char *)grub_efi_allocate_iso_buf(headlen + file->size);
#else
    buf = (char *)grub_malloc(headlen + file->size);
#endif   

    ventoy_fill_os_param(file, (ventoy_os_param *)buf);

    grub_file_read(file, buf + headlen, file->size);

    grub_snprintf(name, sizeof(name), "%s_addr", args[1]);
    grub_snprintf(value, sizeof(value), "0x%llx", (unsigned long long)(unsigned long)buf);
    grub_env_set(name, value);
    
    grub_snprintf(name, sizeof(name), "%s_size", args[1]);
    grub_snprintf(value, sizeof(value), "%llu", (unsigned long long)file->size);
    grub_env_set(name, value);

    grub_file_close(file); 
    rc = 0;
    
    return rc;
}

static grub_err_t ventoy_cmd_iso9660_nojoliet(grub_extcmd_context_t ctxt, int argc, char **args)
{
    (void)ctxt;

    if (argc != 1)
    {
        return 1;
    }

    if (args[0][0] == '1')
    {
        grub_iso9660_set_nojoliet(1);
    }
    else
    {
        grub_iso9660_set_nojoliet(0);
    }

    return 0;
}

static grub_err_t ventoy_cmd_is_udf(grub_extcmd_context_t ctxt, int argc, char **args)
{
    int i;
    int rc = 1;
    grub_file_t file;
    grub_uint8_t buf[32];
    
    (void)ctxt;
    (void)argc;
    (void)args;

    if (argc != 1)
    {
        return rc;
    }

    file = ventoy_grub_file_open(VENTOY_FILE_TYPE, "%s", args[0]);
    if (file == NULL)
    {
        debug("failed to open file <%s> for udf check\n", args[0]);
        return 1;
    }

    for (i = 16; i < 32; i++)
    {
        grub_file_seek(file, i * 2048);
        grub_file_read(file, buf, sizeof(buf));
        if (buf[0] == 255)
        {
            break;
        }
    }

    i++;
    grub_file_seek(file, i * 2048);
    grub_file_read(file, buf, sizeof(buf));

    if (grub_memcmp(buf + 1, "BEA01", 5) == 0)
    {
        i++;
        grub_file_seek(file, i * 2048);
        grub_file_read(file, buf, sizeof(buf));

        if (grub_memcmp(buf + 1, "NSR02", 5) == 0 ||
            grub_memcmp(buf + 1, "NSR03", 5) == 0)
        {
            rc = 0;
        }
    }

    grub_file_close(file); 

    debug("ISO UDF: %s\n", rc ? "NO" : "YES");
    
    return rc;
}

static grub_err_t ventoy_cmd_cmp(grub_extcmd_context_t ctxt, int argc, char **args)
{
    long value_long1 = 0;
    long value_long2 = 0;
    
    if ((argc != 3) || (!ventoy_is_decimal(args[0])) || (!ventoy_is_decimal(args[2])))
    {
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "Usage: %s {Int1} { eq|ne|gt|lt|ge|le } {Int2}", cmd_raw_name);
    }

    value_long1 = grub_strtol(args[0], NULL, 10);
    value_long2 = grub_strtol(args[2], NULL, 10);

    if (0 == grub_strcmp(args[1], "eq"))
    {
        grub_errno = (value_long1 == value_long2) ? GRUB_ERR_NONE : GRUB_ERR_TEST_FAILURE;
    }
    else if (0 == grub_strcmp(args[1], "ne"))
    {
        grub_errno = (value_long1 != value_long2) ? GRUB_ERR_NONE : GRUB_ERR_TEST_FAILURE;
    }
    else if (0 == grub_strcmp(args[1], "gt"))
    {
        grub_errno = (value_long1 > value_long2) ? GRUB_ERR_NONE : GRUB_ERR_TEST_FAILURE;
    }
    else if (0 == grub_strcmp(args[1], "lt"))
    {
        grub_errno = (value_long1 < value_long2) ? GRUB_ERR_NONE : GRUB_ERR_TEST_FAILURE;
    }
    else if (0 == grub_strcmp(args[1], "ge"))
    {
        grub_errno = (value_long1 >= value_long2) ? GRUB_ERR_NONE : GRUB_ERR_TEST_FAILURE;
    }
    else if (0 == grub_strcmp(args[1], "le"))
    {
        grub_errno = (value_long1 <= value_long2) ? GRUB_ERR_NONE : GRUB_ERR_TEST_FAILURE;
    }
    else
    {
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "Usage: %s {Int1} { eq ne gt lt ge le } {Int2}", cmd_raw_name);
    }
    
    return grub_errno;
}

static grub_err_t ventoy_cmd_device(grub_extcmd_context_t ctxt, int argc, char **args)
{
    char *pos = NULL;
    char buf[128] = {0};
    
    if (argc != 2)
    {
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "Usage: %s path var", cmd_raw_name);
    }

    grub_strncpy(buf, (args[0][0] == '(') ? args[0] + 1 : args[0], sizeof(buf) - 1);
    pos = grub_strstr(buf, ",");
    if (pos)
    {
        *pos = 0;
    }

    grub_env_set(args[1], buf);
    
    VENTOY_CMD_RETURN(GRUB_ERR_NONE);
}

static grub_err_t ventoy_cmd_check_compatible(grub_extcmd_context_t ctxt, int argc, char **args)
{
    int i;
    char buf[256];
    grub_disk_t disk;
    char *pos = NULL;
    const char *files[] = { "ventoy.dat", "VENTOY.DAT" };

    (void)ctxt;
    
    if (argc != 1)
    {
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "Usage: %s  (loop)", cmd_raw_name);
    }

    for (i = 0; i < (int)ARRAY_SIZE(files); i++)
    {
        grub_snprintf(buf, sizeof(buf) - 1, "[ -e %s/%s ]", args[0], files[i]);
        if (0 == grub_script_execute_sourcecode(buf))
        {
            debug("file %s exist, ventoy_compatible YES\n", buf);
            grub_env_set("ventoy_compatible", "YES");
            VENTOY_CMD_RETURN(GRUB_ERR_NONE);
        }
        else
        {
            debug("file %s NOT exist\n", buf);
        }
    }
    
    grub_snprintf(buf, sizeof(buf) - 1, "%s", args[0][0] == '(' ? (args[0] + 1) : args[0]);
    pos = grub_strstr(buf, ")");
    if (pos)
    {
        *pos = 0;
    }

    disk = grub_disk_open(buf);
    if (disk)
    {
        grub_disk_read(disk, 16 << 2, 0, 1024, g_img_swap_tmp_buf);
        grub_disk_close(disk);
        
        g_img_swap_tmp_buf[703] = 0;
        for (i = 319; i < 703; i++)
        {
            if (g_img_swap_tmp_buf[i] == 'V' &&
                0 == grub_strncmp(g_img_swap_tmp_buf + i, VENTOY_COMPATIBLE_STR, VENTOY_COMPATIBLE_STR_LEN))
            {
                debug("Ventoy compatible string exist at  %d, ventoy_compatible YES\n", i);
                grub_env_set("ventoy_compatible", "YES");
                VENTOY_CMD_RETURN(GRUB_ERR_NONE);
            }
        }
    }
    else
    {
        debug("failed to open disk <%s>\n", buf);
    }

    grub_env_set("ventoy_compatible", "NO");
    VENTOY_CMD_RETURN(GRUB_ERR_NONE);
}

int ventoy_cmp_img(img_info *img1, img_info *img2)
{
    char *s1, *s2;
    int c1 = 0;
    int c2 = 0;

    for (s1 = img1->name, s2 = img2->name; *s1 && *s2; s1++, s2++)
    {
        c1 = *s1;
        c2 = *s2;

        if (grub_islower(c1))
        {
            c1 = c1 - 'a' + 'A';
        }
        
        if (grub_islower(c2))
        {
            c2 = c2 - 'a' + 'A';
        }

        if (c1 != c2)
        {
            break;
        }
    }

    return (c1 - c2);
}

void ventoy_swap_img(img_info *img1, img_info *img2)
{
    grub_memcpy(&g_img_swap_tmp, img1, sizeof(img_info));
    
    grub_memcpy(img1, img2, sizeof(img_info));
    img1->next = g_img_swap_tmp.next;
    img1->prev = g_img_swap_tmp.prev;

    g_img_swap_tmp.next = img2->next;
    g_img_swap_tmp.prev = img2->prev;
    grub_memcpy(img2, &g_img_swap_tmp, sizeof(img_info));
}

static int ventoy_img_name_valid(const char *filename, grub_size_t namelen)
{
    grub_size_t i;

    if (g_filt_dot_underscore_file && filename[0] == '.' && filename[1] == '_')
    {
        return 0;
    }

    for (i = 0; i < namelen; i++)
    {
        if (filename[i] == ' ' || filename[i] == '\t')
        {
            return 0;
        }

        if ((grub_uint8_t)(filename[i]) >= 127)
        {
            return 0;
        }
    }

    return 1;
}

static int ventoy_check_ignore_flag(const char *filename, const struct grub_dirhook_info *info, void *data)
{
    if (0 == info->dir)
    {
        if (filename && filename[0] == '.' && 0 == grub_strncmp(filename, ".ventoyignore", 13))
        {
            *((int *)data) = 1;
            return 0;
        }
    }

    return 0;
}

static int ventoy_colect_img_files(const char *filename, const struct grub_dirhook_info *info, void *data)
{
    int i = 0;
    int type = 0;
    int ignore = 0;
    grub_size_t len;
    img_info *img;
    img_info *tail;
    img_iterator_node *tmp;
    img_iterator_node *new_node;
    img_iterator_node *node = (img_iterator_node *)data;

    len = grub_strlen(filename);
    
    if (info->dir)
    {
        if ((len == 1 && filename[0] == '.') ||
            (len == 2 && filename[0] == '.' && filename[1] == '.'))
        {
            return 0;
        }

        if (!ventoy_img_name_valid(filename, len))
        {
            return 0;
        }

        if (filename[0] == '$' && 0 == grub_strncmp(filename, "$RECYCLE.BIN", 12))
        {
            return 0;
        }

        new_node = grub_zalloc(sizeof(img_iterator_node));
        if (new_node)
        {
            new_node->dirlen = grub_snprintf(new_node->dir, sizeof(new_node->dir), "%s%s/", node->dir, filename);

            g_enum_fs->fs_dir(g_enum_dev, new_node->dir, ventoy_check_ignore_flag, &ignore);
            if (ignore)
            {
                debug("Directory %s ignored...\n", new_node->dir);
                grub_free(new_node);
                return 0;
            }

            new_node->tail = node->tail;

            new_node->parent = node;
            if (!node->firstchild)
            {
                node->firstchild = new_node;
            }

            if (g_img_iterator_tail)
            {
                g_img_iterator_tail->next = new_node;
                g_img_iterator_tail = new_node;
            }
            else
            {
                g_img_iterator_head.next = new_node;
                g_img_iterator_tail = new_node;
            }
        }
    }
    else
    {
        debug("Find a file %s\n", filename);
        if (len <= 4)
        {
            return 0;
        }

        if (0 == grub_strcasecmp(filename + len - 4, ".iso"))
        {
            type = img_type_iso;
        }
        else if (g_wimboot_enable && (0 == grub_strcasecmp(filename + len - 4, ".wim")))
        {
            type = img_type_wim;
        }
        #ifdef GRUB_MACHINE_EFI
        else if (0 == grub_strcasecmp(filename + len - 4, ".efi"))
        {
            type = img_type_efi;
        }
        #endif
        else if (0 == grub_strcasecmp(filename + len - 4, ".img"))
        {
            if (len == 18 && grub_strncmp(filename, "ventoy_wimboot", 14) == 0)
            {
                return 0;
            }
            type = img_type_img;
        }
        else
        {
            return 0;
        }

        if (g_filt_dot_underscore_file && filename[0] == '.' && filename[1] == '_')
        {
            return 0;
        }
    
        img = grub_zalloc(sizeof(img_info));
        if (img)
        {
            img->type = type;
            grub_snprintf(img->name, sizeof(img->name), "%s", filename);

            for (i = 0; i < (int)len; i++)
            {
                if (filename[i] == ' ' || filename[i] == '\t' || (0 == grub_isprint(filename[i])))
                {
                    img->name[i] = '*';
                    img->unsupport = 1;
                }
            }
            
            img->pathlen = grub_snprintf(img->path, sizeof(img->path), "%s%s", node->dir, img->name);

            img->size = info->size;
            if (0 == img->size)
            {
                img->size = ventoy_grub_get_file_size("%s/%s%s", g_iso_path, node->dir, filename);
            }

            if (img->size < VTOY_FILT_MIN_FILE_SIZE)
            {
                debug("img <%s> size too small %llu\n", img->name, (ulonglong)img->size);
                grub_free(img);
                return 0;
            }
            
            if (g_ventoy_img_list)
            {
                tail = *(node->tail);
                img->prev = tail;
                tail->next = img;
            }
            else
            {
                g_ventoy_img_list = img;
            }
            
            img->id = g_ventoy_img_count;
            img->parent = node;
            if (node && NULL == node->firstiso)
            {
                node->firstiso = img;
            }

            node->isocnt++;
            tmp = node->parent;
            while (tmp)
            {
                tmp->isocnt++;
                tmp = tmp->parent;
            }
            
            *((img_info **)(node->tail)) = img;
            g_ventoy_img_count++;

            img->alias = ventoy_plugin_get_menu_alias(vtoy_alias_image_file, img->path);
            img->class = ventoy_plugin_get_menu_class(vtoy_class_image_file, img->name);
            if (!img->class)
            {
                img->class = g_menu_class[type];
            }
            img->menu_prefix = g_menu_prefix[type];

            debug("Add %s%s to list %d\n", node->dir, filename, g_ventoy_img_count);
        }
    }

    return 0;
}

int ventoy_fill_data(grub_uint32_t buflen, char *buffer)
{
    int len = GRUB_UINT_MAX;
    const char *value = NULL;
    char name[32] = {0};
    char plat[32] = {0};
    char guidstr[32] = {0};
    ventoy_guid guid = VENTOY_GUID;
    const char *fmt1 = NULL;
    const char *fmt2 = NULL;
    const char *fmt3 = NULL;    
    grub_uint32_t *puint = (grub_uint32_t *)name;
    grub_uint32_t *puint2 = (grub_uint32_t *)plat;
    const char fmtdata[]={ 0x39, 0x35, 0x25, 0x00, 0x35, 0x00, 0x23, 0x30, 0x30, 0x30, 0x30, 0x66, 0x66, 0x00 };
    const char fmtcode[]={
        0x22, 0x0A, 0x2B, 0x20, 0x68, 0x62, 0x6F, 0x78, 0x20, 0x7B, 0x0A, 0x20, 0x20, 0x74, 0x6F, 0x70,
        0x20, 0x3D, 0x20, 0x25, 0x73, 0x0A, 0x20, 0x20, 0x6C, 0x65, 0x66, 0x74, 0x20, 0x3D, 0x20, 0x25,
        0x73, 0x0A, 0x20, 0x20, 0x2B, 0x20, 0x6C, 0x61, 0x62, 0x65, 0x6C, 0x20, 0x7B, 0x74, 0x65, 0x78,
        0x74, 0x20, 0x3D, 0x20, 0x22, 0x25, 0x73, 0x20, 0x25, 0x73, 0x25, 0x73, 0x22, 0x20, 0x63, 0x6F,
        0x6C, 0x6F, 0x72, 0x20, 0x3D, 0x20, 0x22, 0x25, 0x73, 0x22, 0x20, 0x61, 0x6C, 0x69, 0x67, 0x6E,
        0x20, 0x3D, 0x20, 0x22, 0x6C, 0x65, 0x66, 0x74, 0x22, 0x7D, 0x0A, 0x7D, 0x0A, 0x22, 0x00
    };

    grub_memset(name, 0, sizeof(name));
    puint[0] = grub_swap_bytes32(0x56454e54);
    puint[3] = grub_swap_bytes32(0x4f4e0000);
    puint[2] = grub_swap_bytes32(0x45525349);
    puint[1] = grub_swap_bytes32(0x4f595f56);
    value = ventoy_get_env(name);

    grub_memset(name, 0, sizeof(name));
    puint[1] = grub_swap_bytes32(0x5f544f50);
    puint[0] = grub_swap_bytes32(0x56544c45);
    fmt1 = ventoy_get_env(name);
    if (!fmt1)
    {
        fmt1 = fmtdata;
    }
    
    grub_memset(name, 0, sizeof(name));
    puint[1] = grub_swap_bytes32(0x5f4c4654);
    puint[0] = grub_swap_bytes32(0x56544c45);
    fmt2 = ventoy_get_env(name);
    
    grub_memset(name, 0, sizeof(name));
    puint[1] = grub_swap_bytes32(0x5f434c52);
    puint[0] = grub_swap_bytes32(0x56544c45);
    fmt3 = ventoy_get_env(name);

    grub_memcpy(guidstr, &guid, sizeof(guid));

    #if defined (GRUB_MACHINE_EFI)
    puint2[0] = grub_swap_bytes32(0x55454649);
    #else
    puint2[0] = grub_swap_bytes32(0x42494f53);
    #endif

    /* Easter egg :) It will be appreciated if you reserve it, but NOT mandatory. */
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wformat-nonliteral"
    len = grub_snprintf(buffer, buflen, fmtcode, 
                        fmt1 ? fmt1 : fmtdata, 
                        fmt2 ? fmt2 : fmtdata + 4, 
                        value ? value : "", plat, guidstr, 
                        fmt3 ? fmt3 : fmtdata + 6);
    #pragma GCC diagnostic pop

    grub_memset(name, 0, sizeof(name));
    puint[0] = grub_swap_bytes32(0x76746f79);
    puint[2] = grub_swap_bytes32(0x656e7365);
    puint[1] = grub_swap_bytes32(0x5f6c6963);
    ventoy_set_env(name, guidstr);

    return len;
}

static img_info * ventoy_get_min_iso(img_iterator_node *node)
{
    img_info *minimg = NULL;
    img_info *img = (img_info *)(node->firstiso);
    
    while (img && (img_iterator_node *)(img->parent) == node)
    {
        if (img->select == 0 && (NULL == minimg || grub_strcmp(img->name, minimg->name) < 0))
        {
            minimg = img;
        }
        img = img->next;
    }

    if (minimg)
    {
        minimg->select = 1;
    }

    return minimg;
}

static img_iterator_node * ventoy_get_min_child(img_iterator_node *node)
{
    img_iterator_node *Minchild = NULL;
    img_iterator_node *child = node->firstchild;

    while (child && child->parent == node)
    {
        if (child->select == 0 && (NULL == Minchild || grub_strcmp(child->dir, Minchild->dir) < 0))
        {
            Minchild = child;
        }
        child = child->next;
    }

    if (Minchild)
    {
        Minchild->select = 1;
    }

    return Minchild;
}

static int ventoy_dynamic_tree_menu(img_iterator_node *node)
{
    int offset = 1;
    img_info *img = NULL;
    const char *dir_class = NULL;
    const char *dir_alias = NULL;
    img_iterator_node *child = NULL;

    if (node->isocnt == 0 || node->done == 1)
    {
        return 0;
    }

    if (node->parent && node->parent->dirlen < node->dirlen)
    {
        offset = node->parent->dirlen;
    }

    if (node == &g_img_iterator_head)
    {
        if (g_default_menu_mode == 0)
        {
            vtoy_ssprintf(g_tree_script_buf, g_tree_script_pos, 
                          "menuentry \"%-10s [Return to ListView]\" --class=\"vtoyret\" VTOY_RET {\n  "
                          "  echo 'return ...' \n"
                          "}\n", "<--");
        }
    }
    else
    {
        node->dir[node->dirlen - 1] = 0;
        dir_class = ventoy_plugin_get_menu_class(vtoy_class_directory, node->dir);
        if (!dir_class)
        {
            dir_class = "vtoydir";
        }

        dir_alias = ventoy_plugin_get_menu_alias(vtoy_alias_directory, node->dir);
        if (dir_alias)
        {
            vtoy_ssprintf(g_tree_script_buf, g_tree_script_pos, 
                          "submenu \"%-10s %s\" --class=\"%s\" {\n", 
                          "DIR", dir_alias, dir_class);
        }
        else
        {
            vtoy_ssprintf(g_tree_script_buf, g_tree_script_pos, 
                          "submenu \"%-10s [%s]\" --class=\"%s\" {\n", 
                          "DIR", node->dir + offset, dir_class);
        }

        vtoy_ssprintf(g_tree_script_buf, g_tree_script_pos, 
                      "menuentry \"%-10s [../]\" --class=\"vtoyret\" VTOY_RET {\n  "
                      "  echo 'return ...' \n"
                      "}\n", "<--");
    }

    while ((child = ventoy_get_min_child(node)) != NULL)
    {
        ventoy_dynamic_tree_menu(child);
    }

    while ((img = ventoy_get_min_iso(node)) != NULL)
    {
        vtoy_ssprintf(g_tree_script_buf, g_tree_script_pos, 
                      "menuentry \"%-10s %s%s\" --class=\"%s\" --id=\"VID_%d\" {\n"
                      "  %s_%s \n" 
                      "}\n", 
                      grub_get_human_size(img->size, GRUB_HUMAN_SIZE_SHORT), 
                      img->unsupport ? "[***********] " : "", 
                      img->alias ? img->alias : img->name, img->class, img->id,
                      img->menu_prefix,
                      img->unsupport ? "unsupport_menuentry" : "common_menuentry");
    }

    if (node != &g_img_iterator_head)
    {
        vtoy_ssprintf(g_tree_script_buf, g_tree_script_pos, "%s", "}\n");
    }

    node->done = 1;
    return 0;    
}

static grub_err_t ventoy_cmd_list_img(grub_extcmd_context_t ctxt, int argc, char **args)
{
    int len;
    grub_fs_t fs;
    grub_device_t dev = NULL;
    img_info *cur = NULL;
    img_info *tail = NULL;
    img_info *default_node = NULL;
    const char *strdata = NULL;
    char *device_name = NULL;
    const char *default_image = NULL;
    int img_len = 0;
    char buf[32];
    img_iterator_node *node = NULL;
    img_iterator_node *tmp = NULL;
    
    (void)ctxt;

    if (argc != 2)
    {
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "Usage: %s {device} {cntvar}", cmd_raw_name);
    }

    if (g_ventoy_img_list || g_ventoy_img_count)
    {
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "Must clear image before list");
    }

    strdata = ventoy_get_env("VTOY_FILT_DOT_UNDERSCORE_FILE");
    if (strdata && strdata[0] == '1' && strdata[1] == 0)
    {
        g_filt_dot_underscore_file = 1;
    }

    device_name = grub_file_get_device_name(args[0]);
    if (!device_name)
    {
        goto fail;
    }

    g_enum_dev = dev = grub_device_open(device_name);
    if (!dev)
    {
        goto fail;        
    }

    g_enum_fs = fs = grub_fs_probe(dev);
    if (!fs)
    {
        goto fail;
    }

    if (ventoy_get_fs_type(fs->name) >= ventoy_fs_max)
    {
        debug("unsupported fs:<%s>\n", fs->name);
        ventoy_set_env("VTOY_NO_ISO_TIP", "unsupported file system");
        goto fail;
    }

    strdata = ventoy_get_env("VTOY_DEFAULT_MENU_MODE");
    if (strdata && strdata[0] == '1')
    {
        g_default_menu_mode = 1;
    }

    grub_memset(&g_img_iterator_head, 0, sizeof(g_img_iterator_head));

    grub_snprintf(g_iso_path, sizeof(g_iso_path), "%s", args[0]);

    strdata = ventoy_get_env("VTOY_DEFAULT_SEARCH_ROOT");
    if (strdata && strdata[0] == '/')
    {
        len = grub_snprintf(g_img_iterator_head.dir, sizeof(g_img_iterator_head.dir) - 1, "%s", strdata);
        if (g_img_iterator_head.dir[len - 1] != '/')
        {
            g_img_iterator_head.dir[len++] = '/';
        }
        g_img_iterator_head.dirlen = len;
    }
    else
    {
        g_img_iterator_head.dirlen = 1;
        grub_strcpy(g_img_iterator_head.dir, "/"); 
    }

    g_img_iterator_head.tail = &tail;

    for (node = &g_img_iterator_head; node; node = node->next)
    {
        fs->fs_dir(dev, node->dir, ventoy_colect_img_files, node);        
    }

    for (node = &g_img_iterator_head; node; node = node->next)
    {
        ventoy_dynamic_tree_menu(node);
    }

    /* free node */
    node = g_img_iterator_head.next;    
    while (node)
    {
        tmp = node->next;
        grub_free(node);
        node = tmp;
    }
    
    /* sort image list by image name */
    for (cur = g_ventoy_img_list; cur; cur = cur->next)
    {
        for (tail = cur->next; tail; tail = tail->next)
        {
            if (ventoy_cmp_img(cur, tail) > 0)
            {
                ventoy_swap_img(cur, tail);
            }
        }
    }

    if (g_default_menu_mode == 1)
    {
        vtoy_ssprintf(g_list_script_buf, g_list_script_pos, 
                      "menuentry \"%s [Return to TreeView]\" --class=\"vtoyret\" VTOY_RET {\n  "
                      "  echo 'return ...' \n"
                      "}\n", "<--");
    }

    if (g_default_menu_mode == 0)
    {
        default_image = ventoy_get_env("VTOY_DEFAULT_IMAGE");        
        if (default_image)
        {
            img_len = grub_strlen(default_image);
        }
    }

    for (cur = g_ventoy_img_list; cur; cur = cur->next)
    {
        vtoy_ssprintf(g_list_script_buf, g_list_script_pos,
                  "menuentry \"%s%s\" --class=\"%s\" --id=\"VID_%d\" {\n"
                  "  %s_%s \n" 
                  "}\n", 
                  cur->unsupport ? "[***********] " : "", 
                  cur->alias ? cur->alias : cur->name, cur->class, cur->id,
                  cur->menu_prefix,
                  cur->unsupport ? "unsupport_menuentry" : "common_menuentry");

        if (g_default_menu_mode == 0 && default_image && default_node == NULL)
        {
            if (img_len == cur->pathlen && grub_strcmp(default_image, cur->path) == 0)
            {
                default_node = cur;
            }
        }
    }

    if (default_node)
    {
        vtoy_ssprintf(g_list_script_buf, g_list_script_pos, "set default='VID_%d'\n", default_node->id);
    }
    
    g_list_script_buf[g_list_script_pos] = 0;

    grub_snprintf(buf, sizeof(buf), "%d", g_ventoy_img_count);
    grub_env_set(args[1], buf);

fail:

    check_free(device_name, grub_free);
    check_free(dev, grub_device_close);

    VENTOY_CMD_RETURN(GRUB_ERR_NONE);
}


static grub_err_t ventoy_cmd_clear_img(grub_extcmd_context_t ctxt, int argc, char **args)
{
    img_info *next = NULL;
    img_info *cur = g_ventoy_img_list;

    (void)ctxt;
    (void)argc;
    (void)args;

    while (cur)
    {
        next = cur->next;
        grub_free(cur);
        cur = next;
    }
    
    g_ventoy_img_list = NULL;
    g_ventoy_img_count = 0;
    
    VENTOY_CMD_RETURN(GRUB_ERR_NONE);
}

static grub_err_t ventoy_cmd_img_name(grub_extcmd_context_t ctxt, int argc, char **args)
{
    long img_id = 0;
    img_info *cur = g_ventoy_img_list;

    (void)ctxt;
    
    if (argc != 2 || (!ventoy_is_decimal(args[0])))
    {
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "Usage: %s {imageID} {var}", cmd_raw_name);
    }

    img_id = grub_strtol(args[0], NULL, 10);
    if (img_id >= g_ventoy_img_count)
    {
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "No such many images %ld %ld", img_id, g_ventoy_img_count);
    }

    debug("Find image %ld name \n", img_id);

    while (cur && img_id > 0)
    {
        img_id--;
        cur = cur->next;
    }

    if (!cur)
    {
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "No such many images");
    }

    debug("image name is %s\n", cur->name);

    grub_env_set(args[1], cur->name);
    
    VENTOY_CMD_RETURN(GRUB_ERR_NONE);
}

static grub_err_t ventoy_cmd_chosen_img_path(grub_extcmd_context_t ctxt, int argc, char **args)
{
    int img_id = 0;
    char value[32];
    char *pos = NULL;
    const char *id = NULL;
    img_info *cur = g_ventoy_img_list;

    (void)ctxt;
    
    if (argc < 1 || argc > 2)
    {
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "Usage: %s {var}", cmd_raw_name);
    }

    id = grub_env_get("chosen");

    pos = grub_strstr(id, "VID_");
    if (pos)
    {
        img_id = (int)grub_strtoul(pos + 4, NULL, 10);
    }
    else
    {
        img_id = (int)grub_strtoul(id, NULL, 10);
    }

    while (cur)
    {
        if (img_id == cur->id)
        {
            break;
        }
        cur = cur->next;
    }

    if (!cur)
    {
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "No such image");
    }

    grub_env_set(args[0], cur->path);

    if (argc > 1)
    {
        grub_snprintf(value, sizeof(value), "%llu", (ulonglong)(cur->size));
        grub_env_set(args[1], value);        
    }

    VENTOY_CMD_RETURN(GRUB_ERR_NONE);
}

int ventoy_get_disk_guid(const char *filename, grub_uint8_t *guid)
{
    grub_disk_t disk;
    char *device_name;
    char *pos;
    char *pos2;
    
    device_name = grub_file_get_device_name(filename);
    if (!device_name)
    {
        return 1;
    }

    pos = device_name;
    if (pos[0] == '(')
    {
        pos++;
    }

    pos2 = grub_strstr(pos, ",");
    if (!pos2)
    {
        pos2 = grub_strstr(pos, ")");
    }
    
    if (pos2)
    {
        *pos2 = 0;
    }

    disk = grub_disk_open(pos);
    if (disk)
    {
        grub_disk_read(disk, 0, 0x180, 16, guid);
        grub_disk_close(disk);
    }
    else
    {
        return 1;
    }

    grub_free(device_name);
    return 0;
}

grub_uint32_t ventoy_get_iso_boot_catlog(grub_file_t file)
{
    eltorito_descriptor desc;

    grub_memset(&desc, 0, sizeof(desc));
    grub_file_seek(file, 17 * 2048);
    grub_file_read(file, &desc, sizeof(desc));

    if (desc.type != 0 || desc.version != 1)
    {
        return 0;
    }

    if (grub_strncmp((char *)desc.id, "CD001", 5) != 0 ||
        grub_strncmp((char *)desc.system_id, "EL TORITO SPECIFICATION", 23) != 0)
    {
        return 0;
    }

    return desc.sector;    
}

int ventoy_has_efi_eltorito(grub_file_t file, grub_uint32_t sector)
{
    int i;
    int x86count = 0;
    grub_uint8_t buf[512];

    grub_file_seek(file, sector * 2048);
    grub_file_read(file, buf, sizeof(buf));

    if (buf[0] == 0x01 && buf[1] == 0xEF)
    {
        debug("%s efi eltorito in Validation Entry\n", file->name);
        return 1;
    }

    if (buf[0] == 0x01 && buf[1] == 0x00)
    {
        x86count++;
    }

    for (i = 64; i < (int)sizeof(buf); i += 32)
    {
        if ((buf[i] == 0x90 || buf[i] == 0x91) && buf[i + 1] == 0xEF)
        {
            debug("%s efi eltorito offset %d 0x%02x\n", file->name, i, buf[i]);
            return 1;
        }

        if ((buf[i] == 0x90 || buf[i] == 0x91) && buf[i + 1] == 0x00 && x86count == 1)
        {
            debug("0x9100 assume %s efi eltorito offset %d 0x%02x\n", file->name, i, buf[i]);
            return 1;
        }
    }

    debug("%s does not contain efi eltorito\n", file->name);
    return 0;
}

void ventoy_fill_os_param(grub_file_t file, ventoy_os_param *param)
{
    char *pos;
    const char *fs = NULL;
    const char *cdprompt = NULL;
    grub_uint32_t i;
    grub_uint8_t  chksum = 0;
    grub_disk_t   disk;

    disk = file->device->disk;
    grub_memcpy(&param->guid, &g_ventoy_guid, sizeof(ventoy_guid));

    param->vtoy_disk_size = disk->total_sectors * (1 << disk->log_sector_size);
    param->vtoy_disk_part_id = disk->partition->number + 1;
    param->vtoy_disk_part_type = ventoy_get_fs_type(file->fs->name);

    pos = grub_strstr(file->name, "/");
    if (!pos)
    {
        pos = file->name;
    }

    grub_snprintf(param->vtoy_img_path, sizeof(param->vtoy_img_path), "%s", pos);
    
    ventoy_get_disk_guid(file->name, param->vtoy_disk_guid);

    param->vtoy_img_size = file->size;

    param->vtoy_reserved[0] = g_ventoy_break_level;
    param->vtoy_reserved[1] = g_ventoy_debug_level;
    
    param->vtoy_reserved[2] = g_ventoy_chain_type;

    /* Windows CD/DVD prompt   0:suppress  1:reserved */
    param->vtoy_reserved[4] = 0;
    if (g_ventoy_chain_type == 1) /* Windows */
    {
        cdprompt = ventoy_get_env("VTOY_WINDOWS_CD_PROMPT");
        if (cdprompt && cdprompt[0] == '1' && cdprompt[1] == 0)
        {
            param->vtoy_reserved[4] = 1;
        }
    }
    
    fs = ventoy_get_env("ventoy_fs_probe");
    if (fs && grub_strcmp(fs, "udf") == 0)
    {
        param->vtoy_reserved[3] = 1;
    }

    /* calculate checksum */
    for (i = 0; i < sizeof(ventoy_os_param); i++)
    {
        chksum += *((grub_uint8_t *)param + i);
    }
    param->chksum = (grub_uint8_t)(0x100 - chksum);

    return;
}

int ventoy_check_block_list(grub_file_t file, ventoy_img_chunk_list *chunklist, grub_disk_addr_t start)
{
    grub_uint32_t i = 0;
    grub_uint64_t total = 0;
    ventoy_img_chunk *chunk = NULL;

    for (i = 0; i < chunklist->cur_chunk; i++)
    {
        chunk = chunklist->chunk + i;
        
        if (chunk->disk_start_sector <= start)
        {
            debug("%u disk start invalid %lu\n", i, (ulong)start);
            return 1;
        }

        total += chunk->disk_end_sector + 1 - chunk->disk_start_sector;
    }

    if (total != ((file->size + 511) / 512))
    {
        debug("Invalid total: %llu %llu\n", (ulonglong)total, (ulonglong)((file->size + 511) / 512));
        return 1;
    }

    return 0;
}

int ventoy_get_block_list(grub_file_t file, ventoy_img_chunk_list *chunklist, grub_disk_addr_t start)
{
    int fs_type;
    int len;
    grub_uint32_t i = 0;
    grub_uint32_t sector = 0;
    grub_uint32_t count = 0;
    grub_off_t size = 0;
    grub_off_t read = 0;

    fs_type = ventoy_get_fs_type(file->fs->name);
    if (fs_type == ventoy_fs_exfat)
    {
        grub_fat_get_file_chunk(start, file, chunklist);        
    }
    else if (fs_type == ventoy_fs_ext)
    {
        grub_ext_get_file_chunk(start, file, chunklist);        
    }
    else
    {
        file->read_hook = (grub_disk_read_hook_t)grub_disk_blocklist_read;
        file->read_hook_data = chunklist;

        for (size = file->size; size > 0; size -= read)
        {
            read = (size > VTOY_SIZE_1GB) ? VTOY_SIZE_1GB : size;
            grub_file_read(file, NULL, read);
        }

        for (i = 0; start > 0 && i < chunklist->cur_chunk; i++)
        {
            chunklist->chunk[i].disk_start_sector += start;
            chunklist->chunk[i].disk_end_sector += start;
        }

        if (ventoy_fs_udf == fs_type)
        {
            for (i = 0; i < chunklist->cur_chunk; i++)
            {
                count = (chunklist->chunk[i].disk_end_sector + 1 - chunklist->chunk[i].disk_start_sector) >> 2;
                chunklist->chunk[i].img_start_sector = sector;
                chunklist->chunk[i].img_end_sector = sector + count - 1;
                sector += count;
            }
        }
    }

    len = (int)grub_strlen(file->name);
    if (grub_strncasecmp(file->name + len - 4, ".img", 4) == 0)
    {
        for (i = 0; i < chunklist->cur_chunk; i++)
        {
            count = chunklist->chunk[i].disk_end_sector + 1 - chunklist->chunk[i].disk_start_sector;
            if (count < 4)
            {
                count = 1;
            }
            else
            {
                count >>= 2;
            }
            
            chunklist->chunk[i].img_start_sector = sector;
            chunklist->chunk[i].img_end_sector = sector + count - 1;
            sector += count;
        }
    }

    return 0;
}

static grub_err_t ventoy_cmd_img_sector(grub_extcmd_context_t ctxt, int argc, char **args)
{
    int rc;
    grub_file_t file;
    grub_disk_addr_t start;
    
    (void)ctxt;
    (void)argc;

    file = ventoy_grub_file_open(VENTOY_FILE_TYPE, "%s", args[0]);
    if (!file)
    {
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "Can't open file %s\n", args[0]); 
    }

    if (g_img_chunk_list.chunk)
    {
        grub_free(g_img_chunk_list.chunk);
    }

    /* get image chunk data */
    grub_memset(&g_img_chunk_list, 0, sizeof(g_img_chunk_list));
    g_img_chunk_list.chunk = grub_malloc(sizeof(ventoy_img_chunk) * DEFAULT_CHUNK_NUM);
    if (NULL == g_img_chunk_list.chunk)
    {
        return grub_error(GRUB_ERR_OUT_OF_MEMORY, "Can't allocate image chunk memoty\n");
    }
    
    g_img_chunk_list.max_chunk = DEFAULT_CHUNK_NUM;
    g_img_chunk_list.cur_chunk = 0;

    start = file->device->disk->partition->start;

    ventoy_get_block_list(file, &g_img_chunk_list, start);

    rc = ventoy_check_block_list(file, &g_img_chunk_list, start);
    grub_file_close(file);
    
    if (rc)
    {
        return grub_error(GRUB_ERR_NOT_IMPLEMENTED_YET, "Unsupported chunk list.\n");
    }

    grub_memset(&g_grub_param->file_replace, 0, sizeof(g_grub_param->file_replace));
    VENTOY_CMD_RETURN(GRUB_ERR_NONE);
}

static grub_err_t ventoy_cmd_sel_auto_install(grub_extcmd_context_t ctxt, int argc, char **args)
{
    int i = 0;
    int pos = 0;
    char *buf = NULL;
    char configfile[128];
    install_template *node = NULL;
        
    (void)ctxt;
    (void)argc;
    (void)args;

    debug("select auto installation argc:%d\n", argc);

    if (argc < 1)
    {
        return 0;
    }

    node = ventoy_plugin_find_install_template(args[0]);
    if (!node)
    {
        debug("Auto install template not found for %s\n", args[0]);
        return 0;
    }

    if (node->autosel >= 0 && node->autosel <= node->templatenum)
    {
        node->cursel = node->autosel - 1;
        debug("Auto install template auto select %d\n", node->autosel);
        return 0;
    }

    buf = (char *)grub_malloc(VTOY_MAX_SCRIPT_BUF);
    if (!buf)
    {
        return 0;
    }

    vtoy_ssprintf(buf, pos, "menuentry \"Boot without auto installation template\" {\n"
                  "  echo %s\n}\n", "123");

    for (i = 0; i < node->templatenum; i++)
    {
        vtoy_ssprintf(buf, pos, "menuentry \"Boot with %s\" {\n"
                  "  echo 123\n}\n",
                  node->templatepath[i].path);
    }

    g_ventoy_menu_esc = 1;
    g_ventoy_suppress_esc = 1;

    grub_snprintf(configfile, sizeof(configfile), "configfile mem:0x%llx:size:%d", (ulonglong)(ulong)buf, pos);
    grub_script_execute_sourcecode(configfile);
    
    g_ventoy_menu_esc = 0;
    g_ventoy_suppress_esc = 0;

    grub_free(buf);

    node->cursel = g_ventoy_last_entry - 1;

    VENTOY_CMD_RETURN(GRUB_ERR_NONE);
}

static grub_err_t ventoy_cmd_sel_persistence(grub_extcmd_context_t ctxt, int argc, char **args)
{
    int i = 0;
    int pos = 0;
    char *buf = NULL;
    char configfile[128];
    persistence_config *node;
    
    (void)ctxt;
    (void)argc;
    (void)args;

    debug("select persistence argc:%d\n", argc);

    if (argc < 1)
    {
        return 0;
    }

    node = ventoy_plugin_find_persistent(args[0]);
    if (!node)
    {
        debug("Persistence image not found for %s\n", args[0]);
        return 0;
    }

    if (node->autosel >= 0 && node->autosel <= node->backendnum)
    {
        node->cursel = node->autosel - 1;
        debug("Persistence image auto select %d\n", node->autosel);
        return 0;
    }

    buf = (char *)grub_malloc(VTOY_MAX_SCRIPT_BUF);
    if (!buf)
    {
        return 0;
    }

    vtoy_ssprintf(buf, pos, "menuentry \"Boot without persistence\" {\n"
                  "  echo %s\n}\n", "123");
    
    for (i = 0; i < node->backendnum; i++)
    {
        vtoy_ssprintf(buf, pos, "menuentry \"Boot with %s\" {\n"
                      "  echo 123\n}\n",
                      node->backendpath[i].path);
        
    }

    g_ventoy_menu_esc = 1;
    g_ventoy_suppress_esc = 1;

    grub_snprintf(configfile, sizeof(configfile), "configfile mem:0x%llx:size:%d", (ulonglong)(ulong)buf, pos);
    grub_script_execute_sourcecode(configfile);
    
    g_ventoy_menu_esc = 0;
    g_ventoy_suppress_esc = 0;

    grub_free(buf);

    node->cursel = g_ventoy_last_entry - 1;

    VENTOY_CMD_RETURN(GRUB_ERR_NONE);
}

static grub_err_t ventoy_cmd_dump_img_sector(grub_extcmd_context_t ctxt, int argc, char **args)
{
    grub_uint32_t i;
    ventoy_img_chunk *cur;
    
    (void)ctxt;
    (void)argc;
    (void)args;

    for (i = 0; i < g_img_chunk_list.cur_chunk; i++)
    {
        cur = g_img_chunk_list.chunk + i;
        grub_printf("image:[%u - %u]   <==>  disk:[%llu - %llu]\n", 
            cur->img_start_sector, cur->img_end_sector,
            (unsigned long long)cur->disk_start_sector, (unsigned long long)cur->disk_end_sector
            );
    }

    VENTOY_CMD_RETURN(GRUB_ERR_NONE);
}

#ifdef GRUB_MACHINE_EFI
static grub_err_t ventoy_cmd_relocator_chaindata(grub_extcmd_context_t ctxt, int argc, char **args)
{
    (void)ctxt;
    (void)argc;
    (void)args;
    return 0;
}
#else
static grub_err_t ventoy_cmd_relocator_chaindata(grub_extcmd_context_t ctxt, int argc, char **args)
{
    int rc = 0;
    ulong chain_len = 0;
    char *chain_data = NULL;
    char *relocator_addr = NULL;
    grub_relocator_chunk_t ch;
    struct grub_relocator *relocator = NULL;
    char envbuf[64] = { 0 };

    (void)ctxt;
    (void)argc;
    (void)args;
    
    if (argc != 2)
    {
        return 1;
    }

    chain_data = (char *)grub_strtoul(args[0], NULL, 16);
    chain_len = grub_strtoul(args[1], NULL, 10);

    relocator = grub_relocator_new ();
    if (!relocator)
    {
        debug("grub_relocator_new failed %p %lu\n", chain_data, chain_len);
        return 1;
    }

    rc = grub_relocator_alloc_chunk_addr (relocator, &ch,
					   0x100000, // GRUB_LINUX_BZIMAGE_ADDR,
					   chain_len);
    if (rc)
    {
        debug("grub_relocator_alloc_chunk_addr failed %d %p %lu\n", rc, chain_data, chain_len);
        grub_relocator_unload (relocator);
        return 1;
    }

    relocator_addr = get_virtual_current_address(ch);

    grub_memcpy(relocator_addr, chain_data, chain_len);
    
    grub_relocator_unload (relocator);

    grub_snprintf(envbuf, sizeof(envbuf), "0x%lx", (unsigned long)relocator_addr);
    grub_env_set("vtoy_chain_relocator_addr", envbuf);

    VENTOY_CMD_RETURN(GRUB_ERR_NONE);
}
#endif

static grub_err_t ventoy_cmd_test_block_list(grub_extcmd_context_t ctxt, int argc, char **args)
{
    grub_uint32_t i;
    grub_file_t file;
    ventoy_img_chunk_list chunklist;
    
    (void)ctxt;
    (void)argc;

    file = ventoy_grub_file_open(VENTOY_FILE_TYPE, "%s", args[0]);
    if (!file)
    {
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "Can't open file %s\n", args[0]); 
    }

    /* get image chunk data */
    grub_memset(&chunklist, 0, sizeof(chunklist));
    chunklist.chunk = grub_malloc(sizeof(ventoy_img_chunk) * DEFAULT_CHUNK_NUM);
    if (NULL == chunklist.chunk)
    {
        return grub_error(GRUB_ERR_OUT_OF_MEMORY, "Can't allocate image chunk memoty\n");
    }
    
    chunklist.max_chunk = DEFAULT_CHUNK_NUM;
    chunklist.cur_chunk = 0;

    ventoy_get_block_list(file, &chunklist, 0);
    
    if (0 != ventoy_check_block_list(file, &chunklist, 0))
    {
        grub_printf("########## UNSUPPORTED ###############\n");
    }

    grub_printf("filesystem: <%s> entry number:<%u>\n", file->fs->name, chunklist.cur_chunk);

    for (i = 0; i < chunklist.cur_chunk; i++)
    {
        grub_printf("%llu+%llu,", (ulonglong)chunklist.chunk[i].disk_start_sector,
            (ulonglong)(chunklist.chunk[i].disk_end_sector + 1 - chunklist.chunk[i].disk_start_sector));
    }

    grub_printf("\n==================================\n");

    for (i = 0; i < chunklist.cur_chunk; i++)
    {
        grub_printf("%2u: [%llu %llu] - [%llu %llu]\n", i, 
            (ulonglong)chunklist.chunk[i].img_start_sector,
            (ulonglong)chunklist.chunk[i].img_end_sector,
            (ulonglong)chunklist.chunk[i].disk_start_sector,
            (ulonglong)chunklist.chunk[i].disk_end_sector
            );
    }

    grub_free(chunklist.chunk);
    grub_file_close(file);

    VENTOY_CMD_RETURN(GRUB_ERR_NONE);
}

static grub_err_t ventoy_cmd_add_replace_file(grub_extcmd_context_t ctxt, int argc, char **args)
{
    int i;
    ventoy_grub_param_file_replace *replace = NULL;
    
    (void)ctxt;
    (void)argc;
    (void)args;

    if (argc >= 2)
    {
        replace = &(g_grub_param->file_replace);
        replace->magic = GRUB_FILE_REPLACE_MAGIC;
            
        replace->old_name_cnt = 0;
        for (i = 0; i < 4 && i + 1 < argc; i++)
        {
            replace->old_name_cnt++;
            grub_snprintf(replace->old_file_name[i], sizeof(replace->old_file_name[i]), "%s", args[i + 1]);
        }
        
        replace->new_file_virtual_id = (grub_uint32_t)grub_strtoul(args[0], NULL, 10);
    }

    VENTOY_CMD_RETURN(GRUB_ERR_NONE);
}

static grub_err_t ventoy_cmd_dump_menu(grub_extcmd_context_t ctxt, int argc, char **args)
{
    (void)ctxt;
    (void)argc;
    (void)args;

    if (argc == 0)
    {
        grub_printf("List Mode: CurLen:%d  MaxLen:%u\n", g_list_script_pos, VTOY_MAX_SCRIPT_BUF);
        grub_printf("%s", g_list_script_buf);
    }
    else
    {
        grub_printf("Tree Mode: CurLen:%d  MaxLen:%u\n", g_tree_script_pos, VTOY_MAX_SCRIPT_BUF);
        grub_printf("%s", g_tree_script_buf);        
    }

    return 0;
}

static grub_err_t ventoy_cmd_dump_img_list(grub_extcmd_context_t ctxt, int argc, char **args)
{
    img_info *cur = g_ventoy_img_list;
        
    (void)ctxt;
    (void)argc;
    (void)args;

    while (cur)
    {
        grub_printf("path:<%s> id=%d\n", cur->path, cur->id);
        grub_printf("name:<%s>\n\n", cur->name);
        cur = cur->next;
    }

    return 0;
}

static grub_err_t ventoy_cmd_dump_injection(grub_extcmd_context_t ctxt, int argc, char **args)
{
    (void)ctxt;
    (void)argc;
    (void)args;

    ventoy_plugin_dump_injection();

    return 0;
}

static grub_err_t ventoy_cmd_dump_auto_install(grub_extcmd_context_t ctxt, int argc, char **args)
{
    (void)ctxt;
    (void)argc;
    (void)args;

    ventoy_plugin_dump_auto_install();

    return 0;
}

static grub_err_t ventoy_cmd_dump_persistence(grub_extcmd_context_t ctxt, int argc, char **args)
{
    (void)ctxt;
    (void)argc;
    (void)args;

    ventoy_plugin_dump_persistence();

    return 0;
}

static grub_err_t ventoy_cmd_check_mode(grub_extcmd_context_t ctxt, int argc, char **args)
{
    (void)ctxt;
    (void)argc;
    (void)args;

    if (argc != 1)
    {
        return 1;
    }

    if (args[0][0] == '0')
    {
        return g_ventoy_memdisk_mode ? 0 : 1;
    }
    else if (args[0][0] == '1')
    {
        return g_ventoy_iso_raw ? 0 : 1;
    }
    else if (args[0][0] == '2')
    {
        return g_ventoy_iso_uefi_drv ? 0 : 1;
    }

    return 1;
}

static grub_err_t ventoy_cmd_dynamic_menu(grub_extcmd_context_t ctxt, int argc, char **args)
{
    static int configfile_mode = 0;
    char memfile[128] = {0};
    
    (void)ctxt;
    (void)argc;
    (void)args;

    /* 
     * args[0]:  0:normal     1:configfile
     * args[1]:  0:list_buf   1:tree_buf
     */

    if (argc != 2)
    {
        debug("Invalid argc %d\n", argc);
        return 0;
    }    

    if (args[0][0] == '0')
    {
        if (args[1][0] == '0')
        {
            grub_script_execute_sourcecode(g_list_script_buf);            
        }
        else
        {
            grub_script_execute_sourcecode(g_tree_script_buf); 
        }
    }
    else
    {
        if (configfile_mode)
        {
            debug("Now already in F3 mode %d\n", configfile_mode);
            return 0;
        }

        if (args[1][0] == '0')
        {
            grub_snprintf(memfile, sizeof(memfile), "configfile mem:0x%llx:size:%d", 
                (ulonglong)(ulong)g_list_script_buf, g_list_script_pos);
        }
        else
        {
             g_ventoy_last_entry = -1;
            grub_snprintf(memfile, sizeof(memfile), "configfile mem:0x%llx:size:%d", 
                (ulonglong)(ulong)g_tree_script_buf, g_tree_script_pos); 
        }

        configfile_mode = 1;
        grub_script_execute_sourcecode(memfile);
        configfile_mode = 0;
    }
    
    return 0;
}

static grub_err_t ventoy_cmd_file_exist_nocase(grub_extcmd_context_t ctxt, int argc, char **args)
{
    grub_file_t file;

    (void)ctxt;

    if (argc != 1)
    {
        return 1;
    }
    
    g_ventoy_case_insensitive = 1;
    file = grub_file_open(args[0], VENTOY_FILE_TYPE);
    g_ventoy_case_insensitive = 0;

    grub_errno = 0;

    if (file)
    {
        grub_file_close(file);
        return 0;
    }
    return 1;
}

static grub_err_t ventoy_cmd_find_bootable_hdd(grub_extcmd_context_t ctxt, int argc, char **args)
{
    int id = 0;
    int find = 0;
    grub_disk_t disk;
    const char *isopath = NULL;
    char hdname[32];
    ventoy_mbr_head mbr;
    
    (void)ctxt;
    (void)argc;

    if (argc != 1)
    {
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "Usage: %s variable\n", cmd_raw_name); 
    }

    isopath = grub_env_get("vtoy_iso_part");
    if (!isopath)
    {
        debug("isopath is null %p\n", isopath);
        return 0;
    }

    debug("isopath is %s\n", isopath);

    for (id = 0; id < 30 && (find == 0); id++)
    {
        grub_snprintf(hdname, sizeof(hdname), "hd%d,", id);
        if (grub_strstr(isopath, hdname))
        {
            debug("skip %s ...\n", hdname);
            continue;
        }

        grub_snprintf(hdname, sizeof(hdname), "hd%d", id);
        
        disk = grub_disk_open(hdname);
        if (!disk)
        {
            debug("%s not exist\n", hdname);
            break;
        }

        grub_memset(&mbr, 0, sizeof(mbr));
        if (0 == grub_disk_read(disk, 0, 0, 512, &mbr))
        {
            if (mbr.Byte55 == 0x55 && mbr.ByteAA == 0xAA)
            {
                if (mbr.PartTbl[0].Active == 0x80 || mbr.PartTbl[1].Active == 0x80 ||
                    mbr.PartTbl[2].Active == 0x80 || mbr.PartTbl[3].Active == 0x80)
                {
                    
                    grub_env_set(args[0], hdname);
                    find = 1;
                }
            }
            debug("%s is %s\n", hdname, find ? "bootable" : "NOT bootable");
        }
        else
        {
            debug("read %s failed\n", hdname);
        }

        grub_disk_close(disk);
    }

    return 0;
}

static grub_err_t ventoy_cmd_read_1st_line(grub_extcmd_context_t ctxt, int argc, char **args)
{
    int len = 1024;
    grub_file_t file;
    char *buf = NULL;
        
    (void)ctxt;
    (void)argc;

    if (argc != 2)
    {
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "Usage: %s file var \n", cmd_raw_name); 
    }

    file = ventoy_grub_file_open(VENTOY_FILE_TYPE, "%s", args[0]);
    if (!file)
    {
        debug("failed to open file %s\n", args[0]);
        return 0;
    }

    buf = grub_malloc(len);
    if (!buf)
    {
        goto end;
    }

    buf[len - 1] = 0;
    grub_file_read(file, buf, len - 1);

    ventoy_get_line(buf);
    ventoy_set_env(args[1], buf);

end:

    grub_check_free(buf);
    grub_file_close(file);
    
    return 0;
}

static int ventoy_img_partition_callback (struct grub_disk *disk, const grub_partition_t partition, void *data)
{
    (void)disk;
    (void)data;

    g_part_list_pos += grub_snprintf(g_part_list_buf + g_part_list_pos, VTOY_MAX_SCRIPT_BUF - g_part_list_pos,
        "0 %llu linear /dev/ventoy %llu\n",
        (ulonglong)partition->len, (ulonglong)partition->start);
        
    return 0;
}

static grub_err_t ventoy_cmd_img_part_info(grub_extcmd_context_t ctxt, int argc, char **args)
{
    char *device_name = NULL;
    grub_device_t dev = NULL;
    char buf[64];
    
    (void)ctxt;

    g_part_list_pos = 0;
    grub_env_unset("vtoy_img_part_file");

    if (argc != 1)
    {
        return 1;
    }

    device_name = grub_file_get_device_name(args[0]);
    if (!device_name)
    {
        debug("ventoy_cmd_img_part_info failed, %s\n", args[0]);
        goto end;
    }

    dev = grub_device_open(device_name);
    if (!dev)
    {
        debug("grub_device_open failed, %s\n", device_name);
        goto end;        
    }

    grub_partition_iterate(dev->disk, ventoy_img_partition_callback, NULL);

    grub_snprintf(buf, sizeof(buf), "newc:vtoy_dm_table:mem:0x%llx:size:%d", (ulonglong)(ulong)g_part_list_buf, g_part_list_pos);
    grub_env_set("vtoy_img_part_file", buf);

end:

    check_free(device_name, grub_free);
    check_free(dev, grub_device_close);
    
    return 0;
}


static grub_err_t ventoy_cmd_file_strstr(grub_extcmd_context_t ctxt, int argc, char **args)
{
    int rc = 1;
    grub_file_t file;
    char *buf = NULL;
        
    (void)ctxt;
    (void)argc;

    if (argc != 2)
    {
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "Usage: %s file str \n", cmd_raw_name); 
    }

    file = ventoy_grub_file_open(VENTOY_FILE_TYPE, "%s", args[0]);
    if (!file)
    {
        debug("failed to open file %s\n", args[0]);
        return 1;
    }

    buf = grub_malloc(file->size + 1);
    if (!buf)
    {
        goto end;
    }

    buf[file->size] = 0;
    grub_file_read(file, buf, file->size);

    if (grub_strstr(buf, args[1]))
    {
        rc = 0;
    }

end:

    grub_check_free(buf);
    grub_file_close(file);
    
    return rc;
}

static grub_err_t ventoy_cmd_parse_volume(grub_extcmd_context_t ctxt, int argc, char **args)
{
    int len;
    grub_file_t file;
    char buf[64];
    ventoy_iso9660_vd pvd;
        
    (void)ctxt;
    (void)argc;

    if (argc != 3)
    {
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "Usage: %s sysid volid \n", cmd_raw_name); 
    }

    file = ventoy_grub_file_open(VENTOY_FILE_TYPE, "%s", args[0]);
    if (!file)
    {
        debug("failed to open file %s\n", args[0]);
        return 0;
    }

    grub_file_seek(file, 16 * 2048);
    len = (int)grub_file_read(file, &pvd, sizeof(pvd));
    if (len != sizeof(pvd))
    {
        debug("failed to read pvd %d\n", len);
        goto end;
    }

    grub_memset(buf, 0, sizeof(buf));
    grub_memcpy(buf, pvd.sys, sizeof(pvd.sys));
    ventoy_set_env(args[1], buf);

    grub_memset(buf, 0, sizeof(buf));
    grub_memcpy(buf, pvd.vol, sizeof(pvd.vol));
    ventoy_set_env(args[2], buf);

end:
    grub_file_close(file);
    
    return 0;
}

static grub_err_t ventoy_cmd_parse_create_date(grub_extcmd_context_t ctxt, int argc, char **args)
{
    int len;
    grub_file_t file;
    char buf[64];
    
    (void)ctxt;
    (void)argc;

    if (argc != 2)
    {
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "Usage: %s var \n", cmd_raw_name); 
    }

    file = ventoy_grub_file_open(VENTOY_FILE_TYPE, "%s", args[0]);
    if (!file)
    {
        debug("failed to open file %s\n", args[0]);
        return 0;
    }

    grub_memset(buf, 0, sizeof(buf));
    grub_file_seek(file, 16 * 2048 + 813);
    len = (int)grub_file_read(file, buf, 17);
    if (len != 17)
    {
        debug("failed to read create date %d\n", len);
        goto end;
    }

    ventoy_set_env(args[1], buf);

end:
    grub_file_close(file);
    
    return 0;
}

static grub_err_t ventoy_cmd_img_hook_root(grub_extcmd_context_t ctxt, int argc, char **args)
{
    (void)ctxt;
    (void)argc;
    (void)args;

    ventoy_env_hook_root(1);
    
    return 0;
}

static grub_err_t ventoy_cmd_img_unhook_root(grub_extcmd_context_t ctxt, int argc, char **args)
{
    (void)ctxt;
    (void)argc;
    (void)args;

    ventoy_env_hook_root(0);
    
    return 0;
}

static grub_err_t ventoy_cmd_push_last_entry(grub_extcmd_context_t ctxt, int argc, char **args)
{
    (void)ctxt;
    (void)argc;
    (void)args;

    g_ventoy_last_entry_back = g_ventoy_last_entry;
    g_ventoy_last_entry = -1;
    
    return 0;
}

static grub_err_t ventoy_cmd_pop_last_entry(grub_extcmd_context_t ctxt, int argc, char **args)
{
    (void)ctxt;
    (void)argc;
    (void)args;

    g_ventoy_last_entry = g_ventoy_last_entry_back;
    
    return 0;
}

static int ventoy_lib_module_callback(const char *filename, const struct grub_dirhook_info *info, void *data)
{
    const char *pos = filename + 1;

    if (info->dir)
    {
        while (*pos)
        {
            if (*pos == '.')
            {
                if ((*(pos - 1) >= '0' && *(pos - 1) <= '9') && (*(pos + 1) >= '0' && *(pos + 1) <= '9'))
                {
                    grub_strncpy((char *)data, filename, 128);
                    return 1;
                }
            }
            pos++;
        }
    }

    return 0;
}

static grub_err_t ventoy_cmd_lib_module_ver(grub_extcmd_context_t ctxt, int argc, char **args)
{
    int rc = 1;
    char *device_name = NULL;
    grub_device_t dev = NULL;
    grub_fs_t fs = NULL;
    char buf[128] = {0};
    
    (void)ctxt;

    if (argc != 3)
    {
        debug("ventoy_cmd_lib_module_ver, invalid param num %d\n", argc);
        return 1;
    }

    debug("ventoy_cmd_lib_module_ver %s %s %s\n", args[0], args[1], args[2]);

    device_name = grub_file_get_device_name(args[0]);
    if (!device_name)
    {
        debug("grub_file_get_device_name failed, %s\n", args[0]);
        goto end;
    }

    dev = grub_device_open(device_name);
    if (!dev)
    {
        debug("grub_device_open failed, %s\n", device_name);
        goto end;        
    }

    fs = grub_fs_probe(dev);
    if (!fs)
    {
        debug("grub_fs_probe failed, %s\n", device_name);
        goto end;
    }

    fs->fs_dir(dev, args[1], ventoy_lib_module_callback, buf);

    if (buf[0])
    {
        ventoy_set_env(args[2], buf);        
    }
    
    rc = 0;
    
end:

    check_free(device_name, grub_free);
    check_free(dev, grub_device_close);
    
    return rc;
}

static grub_err_t ventoy_cmd_get_fs_label(grub_extcmd_context_t ctxt, int argc, char **args)
{
    int rc = 1;
    char *device_name = NULL;
    grub_device_t dev = NULL;
    grub_fs_t fs = NULL;
    char *label = NULL;
    
    (void)ctxt;

    if (argc != 2)
    {
        debug("ventoy_cmd_get_fs_label, invalid param num %d\n", argc);
        return 1;
    }

    device_name = grub_file_get_device_name(args[0]);
    if (!device_name)
    {
        debug("grub_file_get_device_name failed, %s\n", args[0]);
        goto end;
    }

    dev = grub_device_open(device_name);
    if (!dev)
    {
        debug("grub_device_open failed, %s\n", device_name);
        goto end;        
    }

    fs = grub_fs_probe(dev);
    if (!fs)
    {
        debug("grub_fs_probe failed, %s\n", device_name);
        goto end;
    }

    fs->fs_label(dev, &label);
    if (label)
    {
        ventoy_set_env(args[1], label);
        grub_free(label);
    }
    
    rc = 0;
    
end:

    check_free(device_name, grub_free);
    check_free(dev, grub_device_close);
    
    return rc;
}

static int ventoy_fs_enum_1st_file(const char *filename, const struct grub_dirhook_info *info, void *data)
{
    if (!info->dir)
    {
        grub_snprintf((char *)data, 256, "%s", filename);
        return 1;
    }

    return 0;
}


static grub_err_t ventoy_cmd_fs_enum_1st_file(grub_extcmd_context_t ctxt, int argc, char **args)
{
    int rc = 1;
    char *device_name = NULL;
    grub_device_t dev = NULL;
    grub_fs_t fs = NULL;
    char name[256] ={0};
    
    (void)ctxt;

    if (argc != 3)
    {
        debug("ventoy_cmd_fs_enum_1st_file, invalid param num %d\n", argc);
        return 1;
    }

    device_name = grub_file_get_device_name(args[0]);
    if (!device_name)
    {
        debug("grub_file_get_device_name failed, %s\n", args[0]);
        goto end;
    }

    dev = grub_device_open(device_name);
    if (!dev)
    {
        debug("grub_device_open failed, %s\n", device_name);
        goto end;        
    }

    fs = grub_fs_probe(dev);
    if (!fs)
    {
        debug("grub_fs_probe failed, %s\n", device_name);
        goto end;
    }

    fs->fs_dir(dev, args[1], ventoy_fs_enum_1st_file, name);
    if (name[0])
    {
        ventoy_set_env(args[2], name);
    }
    
    rc = 0;
    
end:

    check_free(device_name, grub_free);
    check_free(dev, grub_device_close);
    
    return rc;
}

static grub_err_t ventoy_cmd_basename(grub_extcmd_context_t ctxt, int argc, char **args)
{
    char c;
    char *pos = NULL;
    char *end = NULL;
    
    (void)ctxt;

    if (argc != 2)
    {
        debug("ventoy_cmd_basename, invalid param num %d\n", argc);
        return 1;
    }

    for (pos = args[0]; *pos; pos++)
    {
        if (*pos == '.')
        {
            end = pos;
        }
    }

    if (end)
    {
        c = *end;
        *end = 0;
    }

    grub_env_set(args[1], args[0]);

    if (end)
    {
        *end = c;
    }

    return 0;
}

grub_uint64_t ventoy_grub_get_file_size(const char *fmt, ...)
{
    grub_uint64_t size = 0;
    grub_file_t file;
    va_list ap;
    char fullpath[256] = {0};

    va_start (ap, fmt);
    grub_vsnprintf(fullpath, 255, fmt, ap);
    va_end (ap);
    
    file = grub_file_open(fullpath, VENTOY_FILE_TYPE);
    if (!file)
    {
        debug("grub_file_open failed <%s>\n", fullpath);
        grub_errno = 0;
        return 0;
    }

    size = file->size;
    grub_file_close(file);
    return size;
}

grub_file_t ventoy_grub_file_open(enum grub_file_type type, const char *fmt, ...)
{
    va_list ap;
    grub_file_t file;
    char fullpath[256] = {0};

    va_start (ap, fmt);
    grub_vsnprintf(fullpath, 255, fmt, ap);
    va_end (ap);

    file = grub_file_open(fullpath, type);
    if (!file)
    {
        debug("grub_file_open failed <%s> %d\n", fullpath, grub_errno);
        grub_errno = 0;
    }

    return file;
}

int ventoy_is_file_exist(const char *fmt, ...)
{
    va_list ap;
    int len;
    char *pos = NULL;
    char buf[256] = {0};

    grub_snprintf(buf, sizeof(buf), "%s", "[ -f ");
    pos = buf + 5;

    va_start (ap, fmt);
    len = grub_vsnprintf(pos, 255, fmt, ap);
    va_end (ap);

    grub_strncpy(pos + len, " ]", 2);

    debug("script exec %s\n", buf);

    if (0 == grub_script_execute_sourcecode(buf))
    {
        return 1;
    }

    return 0;
}

int ventoy_is_dir_exist(const char *fmt, ...)
{
    va_list ap;
    int len;
    char *pos = NULL;
    char buf[256] = {0};

    grub_snprintf(buf, sizeof(buf), "%s", "[ -d ");
    pos = buf + 5;

    va_start (ap, fmt);
    len = grub_vsnprintf(pos, 255, fmt, ap);
    va_end (ap);

    grub_strncpy(pos + len, " ]", 2);

    debug("script exec %s\n", buf);

    if (0 == grub_script_execute_sourcecode(buf))
    {
        return 1;
    }

    return 0;
}

static int ventoy_env_init(void)
{
    char buf[64];

    grub_env_set("vtdebug_flag", "");

    g_part_list_buf = grub_malloc(VTOY_PART_BUF_LEN);
    g_tree_script_buf = grub_malloc(VTOY_MAX_SCRIPT_BUF);
    g_list_script_buf = grub_malloc(VTOY_MAX_SCRIPT_BUF);

    ventoy_filt_register(0, ventoy_wrapper_open);

    g_grub_param = (ventoy_grub_param *)grub_zalloc(sizeof(ventoy_grub_param));
    if (g_grub_param)
    {
        g_grub_param->grub_env_get = grub_env_get;
        g_grub_param->grub_env_set = (grub_env_set_pf)grub_env_set;
        g_grub_param->grub_env_printf = (grub_env_printf_pf)grub_printf;
        grub_snprintf(buf, sizeof(buf), "%p", g_grub_param);
        grub_env_set("env_param", buf);
    }

    return 0;
}

static cmd_para ventoy_cmds[] = 
{
    { "vt_incr",  ventoy_cmd_incr,  0, NULL, "{Var} {INT}",   "Increase integer variable",    NULL },
    { "vt_strstr",  ventoy_cmd_strstr,  0, NULL, "",   "",    NULL },
    { "vt_str_begin",  ventoy_cmd_strbegin,  0, NULL, "",   "",    NULL },
    { "vt_debug", ventoy_cmd_debug, 0, NULL, "{on|off}",   "turn debug on/off",    NULL },
    { "vtdebug", ventoy_cmd_debug, 0, NULL, "{on|off}",   "turn debug on/off",    NULL },
    { "vtbreak", ventoy_cmd_break, 0, NULL, "{level}",   "set debug break",    NULL },
    { "vt_cmp",   ventoy_cmd_cmp, 0, NULL, "{Int1} { eq|ne|gt|lt|ge|le } {Int2}", "Comare two integers", NULL },
    { "vt_device", ventoy_cmd_device, 0, NULL, "path var", "", NULL },
    { "vt_check_compatible",   ventoy_cmd_check_compatible, 0, NULL, "", "", NULL },
    { "vt_list_img", ventoy_cmd_list_img, 0, NULL, "{device} {cntvar}", "find all iso file in device", NULL },
    { "vt_clear_img", ventoy_cmd_clear_img, 0, NULL, "", "clear image list", NULL },
    { "vt_img_name", ventoy_cmd_img_name, 0, NULL, "{imageID} {var}", "get image name", NULL },
    { "vt_chosen_img_path", ventoy_cmd_chosen_img_path, 0, NULL, "{var}", "get chosen img path", NULL },
    { "vt_img_sector", ventoy_cmd_img_sector, 0, NULL, "{imageName}", "", NULL },
    { "vt_dump_img_sector", ventoy_cmd_dump_img_sector, 0, NULL, "", "", NULL },
    { "vt_load_wimboot", ventoy_cmd_load_wimboot, 0, NULL, "", "", NULL },

    { "vt_cpio_busybox64", ventoy_cmd_cpio_busybox_64, 0, NULL, "", "", NULL },
    { "vt_load_cpio", ventoy_cmd_load_cpio, 0, NULL, "", "", NULL },
    { "vt_trailer_cpio", ventoy_cmd_trailer_cpio, 0, NULL, "", "", NULL },
    { "vt_push_last_entry", ventoy_cmd_push_last_entry, 0, NULL, "", "", NULL },
    { "vt_pop_last_entry", ventoy_cmd_pop_last_entry, 0, NULL, "", "", NULL },
    { "vt_get_lib_module_ver", ventoy_cmd_lib_module_ver, 0, NULL, "", "", NULL },

    { "vt_get_fs_label", ventoy_cmd_get_fs_label, 0, NULL, "", "", NULL },
    { "vt_fs_enum_1st_file", ventoy_cmd_fs_enum_1st_file, 0, NULL, "", "", NULL },
    { "vt_file_basename", ventoy_cmd_basename, 0, NULL, "", "", NULL },
    

    
    { "vt_find_first_bootable_hd", ventoy_cmd_find_bootable_hdd, 0, NULL, "", "", NULL },
    { "vt_dump_menu", ventoy_cmd_dump_menu, 0, NULL, "", "", NULL },
    { "vt_dynamic_menu", ventoy_cmd_dynamic_menu, 0, NULL, "", "", NULL },
    { "vt_check_mode", ventoy_cmd_check_mode, 0, NULL, "", "", NULL },
    { "vt_dump_img_list", ventoy_cmd_dump_img_list, 0, NULL, "", "", NULL },
    { "vt_dump_injection", ventoy_cmd_dump_injection, 0, NULL, "", "", NULL },
    { "vt_dump_auto_install", ventoy_cmd_dump_auto_install, 0, NULL, "", "", NULL },
    { "vt_dump_persistence", ventoy_cmd_dump_persistence, 0, NULL, "", "", NULL },
    { "vt_select_auto_install", ventoy_cmd_sel_auto_install, 0, NULL, "", "", NULL },
    { "vt_select_persistence", ventoy_cmd_sel_persistence, 0, NULL, "", "", NULL },

    { "vt_iso9660_nojoliet", ventoy_cmd_iso9660_nojoliet, 0, NULL, "", "", NULL },
    { "vt_is_udf", ventoy_cmd_is_udf, 0, NULL, "", "", NULL },
    { "vt_file_size", ventoy_cmd_file_size, 0, NULL, "", "", NULL },
    { "vt_load_file_to_mem", ventoy_cmd_load_file_to_mem, 0, NULL, "", "", NULL },
    { "vt_load_img_memdisk", ventoy_cmd_load_img_memdisk, 0, NULL, "", "", NULL },
    { "vt_concat_efi_iso", ventoy_cmd_concat_efi_iso, 0, NULL, "", "", NULL },
    
    { "vt_linux_parse_initrd_isolinux", ventoy_cmd_isolinux_initrd_collect, 0, NULL, "{cfgfile}", "", NULL },
    { "vt_linux_parse_initrd_grub", ventoy_cmd_grub_initrd_collect, 0, NULL, "{cfgfile}", "", NULL },
    { "vt_linux_specify_initrd_file", ventoy_cmd_specify_initrd_file, 0, NULL, "", "", NULL },
    { "vt_linux_clear_initrd", ventoy_cmd_clear_initrd_list, 0, NULL, "", "", NULL },
    { "vt_linux_dump_initrd", ventoy_cmd_dump_initrd_list, 0, NULL, "", "", NULL },
    { "vt_linux_initrd_count", ventoy_cmd_initrd_count, 0, NULL, "", "", NULL },
    { "vt_linux_valid_initrd_count", ventoy_cmd_valid_initrd_count, 0, NULL, "", "", NULL },
    { "vt_linux_locate_initrd", ventoy_cmd_linux_locate_initrd, 0, NULL, "", "", NULL },
    { "vt_linux_chain_data", ventoy_cmd_linux_chain_data, 0, NULL, "", "", NULL },
    { "vt_linux_get_main_initrd_index", ventoy_cmd_linux_get_main_initrd_index, 0, NULL, "", "", NULL },

    { "vt_windows_reset",      ventoy_cmd_wimdows_reset, 0, NULL, "", "", NULL },
    { "vt_windows_chain_data", ventoy_cmd_windows_chain_data, 0, NULL, "", "", NULL },
    { "vt_windows_collect_wim_patch", ventoy_cmd_collect_wim_patch, 0, NULL, "", "", NULL },
    { "vt_windows_locate_wim_patch", ventoy_cmd_locate_wim_patch, 0, NULL, "", "", NULL },
    { "vt_windows_count_wim_patch", ventoy_cmd_wim_patch_count, 0, NULL, "", "", NULL },
    { "vt_dump_wim_patch", ventoy_cmd_dump_wim_patch, 0, NULL, "", "", NULL },
    { "vt_wim_chain_data", ventoy_cmd_wim_chain_data, 0, NULL, "", "", NULL },

    { "vt_add_replace_file", ventoy_cmd_add_replace_file, 0, NULL, "", "", NULL },
    { "vt_relocator_chaindata", ventoy_cmd_relocator_chaindata, 0, NULL, "", "", NULL },
    { "vt_test_block_list", ventoy_cmd_test_block_list, 0, NULL, "", "", NULL },
    { "vt_file_exist_nocase", ventoy_cmd_file_exist_nocase, 0, NULL, "", "", NULL },

    
    { "vt_load_plugin", ventoy_cmd_load_plugin, 0, NULL, "", "", NULL },
    { "vt_check_plugin_json", ventoy_cmd_plugin_check_json, 0, NULL, "", "", NULL },
    
    { "vt_1st_line", ventoy_cmd_read_1st_line, 0, NULL, "", "", NULL },
    { "vt_file_strstr", ventoy_cmd_file_strstr, 0, NULL, "", "", NULL },
    { "vt_img_part_info", ventoy_cmd_img_part_info, 0, NULL, "", "", NULL },

    
    { "vt_parse_iso_volume", ventoy_cmd_parse_volume, 0, NULL, "", "", NULL },
    { "vt_parse_iso_create_date", ventoy_cmd_parse_create_date, 0, NULL, "", "", NULL },
    { "vt_parse_freenas_ver", ventoy_cmd_parse_freenas_ver, 0, NULL, "", "", NULL },
    { "vt_unix_parse_freebsd_ver", ventoy_cmd_unix_freebsd_ver, 0, NULL, "", "", NULL },
    { "vt_unix_reset", ventoy_cmd_unix_reset, 0, NULL, "", "", NULL },
    { "vt_unix_replace_conf", ventoy_cmd_unix_replace_conf, 0, NULL, "", "", NULL },
    { "vt_unix_replace_ko", ventoy_cmd_unix_replace_ko, 0, NULL, "", "", NULL },
    { "vt_unix_chain_data", ventoy_cmd_unix_chain_data, 0, NULL, "", "", NULL },

    { "vt_img_hook_root", ventoy_cmd_img_hook_root, 0, NULL, "", "", NULL },
    { "vt_img_unhook_root", ventoy_cmd_img_unhook_root, 0, NULL, "", "", NULL },

};



GRUB_MOD_INIT(ventoy)
{
    grub_uint32_t i;
    cmd_para *cur = NULL;

    ventoy_env_init();
    
    for (i = 0; i < ARRAY_SIZE(ventoy_cmds); i++)
    {
        cur = ventoy_cmds + i;
        cur->cmd = grub_register_extcmd(cur->name, cur->func, cur->flags, 
                                        cur->summary, cur->description, cur->parser);
    }
}

GRUB_MOD_FINI(ventoy)
{
    grub_uint32_t i;
    
    for (i = 0; i < ARRAY_SIZE(ventoy_cmds); i++)
    {
        grub_unregister_extcmd(ventoy_cmds[i].cmd);
    }
}

