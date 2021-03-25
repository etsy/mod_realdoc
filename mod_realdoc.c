/*
 Copyright (c) 2013 Etsy

 Permission is hereby granted, free of charge, to any person
 obtaining a copy of this software and associated documentation
 files (the "Software"), to deal in the Software without
 restriction, including without limitation the rights to use,
 copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following
 conditions:

 The above copyright notice and this permission notice shall be
 included in all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 OTHER DEALINGS IN THE SOFTWARE.
*/

#define CORE_PRIVATE

#include "httpd.h"
#include "http_config.h"
#include "http_core.h"
#include "http_protocol.h"
#include "http_log.h"
#include "apr_strings.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char **docroot;
    const char *original;
} realdoc_request_save_struct;

typedef struct {
    apr_time_t realpath_every;
    unsigned int use_readlink;
    const char *prefix_path;
} realdoc_config_struct;

#define AP_LOG_DEBUG(rec, fmt, ...) ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, rec, "[realdoc] " fmt, ##__VA_ARGS__)
#define AP_LOG_ERROR(rec, fmt, ...) ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, rec, "[realdoc] " fmt, ##__VA_ARGS__)

module AP_MODULE_DECLARE_DATA realdoc_module;

static void *create_realdoc_config(apr_pool_t *p, server_rec *d)
{
    return apr_pcalloc(p, sizeof(realdoc_config_struct));
}

static void *merge_realdoc_config(apr_pool_t *p, void *basev, void *addv)
{
    realdoc_config_struct *base = (realdoc_config_struct *) basev;
    realdoc_config_struct *add = (realdoc_config_struct *) addv;
    realdoc_config_struct *new = (realdoc_config_struct *)
                            apr_palloc(p, sizeof(realdoc_config_struct));

    new->realpath_every = add->realpath_every ? add->realpath_every : base->realpath_every;
    new->use_readlink = add->use_readlink ? add->use_readlink : base->use_readlink;
    new->prefix_path = add->prefix_path ? add->prefix_path : base->prefix_path;
    return new;
}

static const char *set_realdoc_config(cmd_parms *cmd, void *dummy, const char *arg)
{
    char *endptr = NULL;
    realdoc_config_struct *conf = (realdoc_config_struct *) ap_get_module_config(cmd->server->module_config,
                      &realdoc_module);

    if (!conf) {
        return apr_psprintf(cmd->pool,
                     "realdoc configuration not initialized");
    }

    conf->realpath_every = strtol(arg, &endptr, 10);

    if ((*arg == '\0') || (*endptr != '\0')) {
        return apr_psprintf(cmd->pool,
                     "Invalid value for realdoc directive %s, expected integer",
                     cmd->directive->directive);
    }

    return NULL;
}

/*
 * Return path with first symlink resolved, if any.
 * Buffer buf must be null-terminated and can be
 * pre-filled with the path to skip ahead to avoid
 * having to stat those path components.
 */
int first_link(char *path, char *buf)
{
    struct stat st;
    char *p = path;

    if (!*path) return 0;
    if (*path != '/') {
        errno = ENOENT;
        return -1;
    }

    if (strlen(buf)) {
        char *skip = strstr(path, buf);
        if (skip == path) {
            p = path + strlen(buf) - 1;
        } else {
            *buf = '\0';
        }
    }

    while((p = strchr(p+1, '/'))) {
        int bytes;
        *p = '\0';
        strcpy(buf, path);
        *p = '/';
        if (lstat(buf, &st) < 0) return -1;
        if (S_ISLNK(st.st_mode)) {
            char lbuf[PATH_MAX];
            if ((bytes = readlink(buf, lbuf, sizeof(lbuf))) < 0) return -1;
            lbuf[bytes] = '\0';
            if (lbuf[0] == '/') {
                strncpy(buf, lbuf, bytes+1);
            } else {
                // For a relative symlink backtrack and replace
                char *pb = strchr(buf, '\0');
                while (*--pb != '/');
                *++pb = '\0';
                strncat(buf, lbuf, bytes+1);
            }
            strcat(buf, p);
            return 0;
        }
    }

    // No symlinks were found, just return the original path in the buffer
    strcpy(buf, path);
    return 0;
}

static const char *set_realdoc_readlink(cmd_parms *cmd, void *dummy, const char *arg)
{
    realdoc_config_struct *conf = (realdoc_config_struct *) ap_get_module_config(cmd->server->module_config,
                      &realdoc_module);

    if (!conf) {
        return apr_psprintf(cmd->pool,
                     "realdoc configuration not initialized");
    }

    if (*arg == '/') {
        conf->use_readlink = 1;
        conf->prefix_path = arg;
    } else if (strcasecmp(arg, "On") == 0) {
        conf->use_readlink = 1;
        conf->prefix_path = NULL;
    } else {
        conf->use_readlink = 0;
    }

    return NULL;
}

static const command_rec realdoc_cmds[] =
{
    AP_INIT_TAKE1("RealpathEvery", set_realdoc_config, NULL, RSRC_CONF,
     "Run the realpath at most every so many seconds"),
    AP_INIT_TAKE1("UseReadlink", set_realdoc_readlink, NULL, RSRC_CONF,
     "Use readlink instead of realpath to just get the first symlink target (On|Off|<path>)"),
    {NULL}
};

static apr_status_t realdoc_restore_docroot(void *data) {
    realdoc_request_save_struct *save = (realdoc_request_save_struct *)data;
    *save->docroot  = save->original;
    return APR_SUCCESS;
}

static int realdoc_hook_handler(request_rec *r) {
    core_server_config *core_conf;
    realdoc_config_struct *realdoc_conf;
    realdoc_request_save_struct *save;
    char *last_saved_real_docroot;
    apr_time_t *last_saved_real_time;
    apr_time_t current_request_time;
    const char *last_saved_docroot_key = apr_psprintf(r->pool, "%s:%u:realdoc_saved_docroot", ap_get_server_name(r), ap_get_server_port(r));
    const char *last_saved_time_key = apr_psprintf(r->pool, "%s:%u:realdoc_saved_time", ap_get_server_name(r), ap_get_server_port(r));

    core_conf = ap_get_module_config(r->server->module_config, &core_module);
    realdoc_conf = (realdoc_config_struct *) ap_get_module_config(r->server->module_config, &realdoc_module);

    apr_pool_userdata_get((void **) &last_saved_real_docroot, last_saved_docroot_key, r->server->process->pool);

    /* Guard against being run a second time on the same request */
    if(core_conf->ap_document_root == last_saved_real_docroot) {
        return DECLINED;
    }

    /* Grab the current docroot address and value and save it
       so we can restore it in our cleanup func */
    save = apr_pcalloc(r->pool, sizeof(realdoc_request_save_struct));
    save->docroot = &core_conf->ap_document_root;
    save->original = core_conf->ap_document_root;

    apr_pool_cleanup_register(r->pool, save, realdoc_restore_docroot, realdoc_restore_docroot);
    current_request_time = apr_time_sec(r->request_time);

    apr_pool_userdata_get((void **) &last_saved_real_time, last_saved_time_key, r->server->process->pool);
    if (!last_saved_real_time) {
        last_saved_real_time = apr_pcalloc(r->server->process->pool, sizeof(apr_time_t));
        if (!last_saved_real_time) {
            AP_LOG_ERROR(r, "Failed to allocate memory for saving time data");
            return DECLINED;
        } else {
            apr_pool_userdata_set((const void *) last_saved_real_time, last_saved_time_key, NULL,
                    r->server->process->pool);
        }
    }

    if (!last_saved_real_docroot) {
        last_saved_real_docroot = apr_pcalloc(r->server->process->pool, PATH_MAX * sizeof(char));
        if (!last_saved_real_docroot) {
            AP_LOG_ERROR(r, "Failed to allocate memory for saving docroot data");
            return DECLINED;
        } else {
            apr_pool_userdata_set((const void *) last_saved_real_docroot, last_saved_docroot_key, NULL,
                    r->server->process->pool);
        }
    }

    if (*last_saved_real_time < (current_request_time - realdoc_conf->realpath_every)) {
        if (realdoc_conf->use_readlink) {
            if (realdoc_conf->prefix_path) {
                strcpy(last_saved_real_docroot, realdoc_conf->prefix_path);
            }
            AP_LOG_DEBUG(r, "PID %d calling first_link(). Original docroot: %s. Buffer: %s", getpid(), core_conf->ap_document_root, last_saved_real_docroot);
            if (-1 == first_link((char *)core_conf->ap_document_root, last_saved_real_docroot)) {
                AP_LOG_ERROR(r, "Error from readlink: %d. Original docroot: %s", errno, core_conf->ap_document_root);
                return DECLINED;
            }
        } else {
            if (NULL == realpath(core_conf->ap_document_root, last_saved_real_docroot)) {
                AP_LOG_ERROR(r, "Error from realpath: %d. Original docroot: %s", errno, core_conf->ap_document_root);
                return DECLINED;
            }
        }

        if (realdoc_conf->use_readlink) {
            AP_LOG_DEBUG(r, "PID %d calling readlink. Original docroot: %s. Resolved: %s", getpid(), core_conf->ap_document_root, last_saved_real_docroot);
        } else {
            AP_LOG_DEBUG(r, "PID %d calling realpath. Original docroot: %s. Resolved: %s", getpid(), core_conf->ap_document_root, last_saved_real_docroot);
        }
        *last_saved_real_time = current_request_time;
    }

    core_conf->ap_document_root = last_saved_real_docroot;
    return DECLINED;
}

void realdoc_register_hook(apr_pool_t *p) {
    ap_hook_post_read_request(realdoc_hook_handler, NULL, NULL, APR_HOOK_REALLY_FIRST);
}

AP_MODULE_DECLARE_DATA module realdoc_module = {
    STANDARD20_MODULE_STUFF,
    NULL,                   /* create per-directory config structure */
    NULL,                   /* merge per-directory config structures */
    create_realdoc_config,  /* create per-server config structure */
    merge_realdoc_config,   /* merge per-server config structures */
    realdoc_cmds,           /* command apr_table_t */
    realdoc_register_hook   /* register hooks */
};
