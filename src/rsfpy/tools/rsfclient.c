/*
  rsfclient_gtk.c

  GTK4 RSF SVG receiver + SSH reverse tunnel client.

  Build in MSYS2 UCRT64:

      cc rsfclient_gtk.c -O2 -o rsfclient.exe \
          $(pkg-config --cflags --libs gtk4 gio-2.0 glib-2.0)

  Run local GUI:

      rsfclient.exe

  Remote sender:

      sfspike n1=10 | rsfgraph | rsfclient --send --port 17890

  Protocol compatible with the previous Python rsfclient_visual_v2.py:
      MAGIC[8] = "RSFVIEW2"
      uint16 token_len, big endian
      token bytes
      uint32 meta_len, big endian
      meta JSON bytes
      uint64 payload_size, big endian
      payload bytes
*/

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef G_OS_WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

#define APP_VERSION "gtk-client-v4-cancel-2026-06-19"
#define MAGIC "RSFVIEW2"
#define MAGIC_LEN 8
#define DEFAULT_PORT 17890
#define RANDOM_PORT_MIN 49152
#define RANDOM_PORT_MAX 65535
#define MAX_PAYLOAD ((guint64)1024 * 1024 * 1024)
#define MAX_RETRIES 3
#define RETRY_DELAY_SEC 3

static gchar *self_exe_path = NULL;

typedef struct _Profile Profile;
typedef struct _Runtime Runtime;
typedef struct _AppState AppState;

#ifdef G_OS_WIN32
typedef struct {
    PROCESS_INFORMATION pi;
    gint stdout_fd;
} WinSshProcess;

typedef struct {
    Runtime *rt;
    gint fd;
} WinSshOutputData;
#endif

struct _Profile {
    gchar *name;
    gchar *ssh_cmd;
    gchar *user;
    gchar *host;
    gint ssh_port;
    gchar *auth_method;
    gchar *password;
    gchar *identity_file;

    gchar *local_host;
    gint local_port;

    gchar *remote_bind_host;
    gint remote_port;

    gchar *viewer_cmd;
    gchar *backend;
    gchar *save_dir;
    gboolean cleanup_temp_files;
    gchar *extra_ssh_args;

    gchar *token_salt;
    gchar *token_hash;
};

struct _Runtime {
    AppState *app;
    Profile *profile;

    GSocketListener *listener;
    GCancellable *cancel;
    GThread *listener_thread;
    GThread *tunnel_thread;

    GMutex lock;
    gboolean stopping;
    gboolean auth_failed;
    gboolean pair_uploaded;
    gboolean tunnel_ready;
    gchar *auth_failure_reason;
    gchar *askpass_cancel_path;
    gchar *pair_token;
    GSubprocess *ssh_proc;
#ifdef G_OS_WIN32
    WinSshProcess *win_ssh_proc;
#endif
};

struct _AppState {
    GtkApplication *gtk_app;
    GtkWidget *window;
    GtkWidget *main_box;

    GKeyFile *config;
    gchar *config_path;

    GtkWidget *profile_list;
    GPtrArray *profiles;

    GtkWidget *log_view;
    GtkTextBuffer *log_buffer;

    Runtime *runtime;

    /* form widgets */
    GHashTable *form_entries;
    GtkWidget *form_backend_combo;
    GtkWidget *form_auth_combo;
    GtkWidget *form_auth_default_box;
    GtkWidget *form_auth_password_box;
    GtkWidget *form_auth_identity_box;
    GtkWidget *form_password_entry;
    GtkWidget *form_save_password;
    GtkWidget *form_keep_password;
    GtkWidget *form_advanced_box;
    GtkWidget *form_cleanup_temp_files;
    GtkWidget *form_keep_token;
    GtkWidget *form_token_entry;
    GtkWidget *main_connect_button;
    GtkWidget *main_edit_button;
    GtkWidget *main_delete_button;
    Profile *editing_profile;
};

/* ---------------------------------------------------------------------- */
/* Utility                                                                 */
/* ---------------------------------------------------------------------- */

static gchar *strdup0(const gchar *s)
{
    return g_strdup(s ? s : "");
}

static gboolean str_empty(const gchar *s)
{
    return (NULL == s || '\0' == *s);
}

static gboolean viewer_command_wants_backend_arg(const gchar *viewer_cmd,
                                                  const gchar *backend)
{
    gchar **argv = NULL;
    gchar *base = NULL;
    gboolean wants = FALSE;
    gint argc = 0;

    if (str_empty(backend) || g_strcmp0(backend, "none") == 0) return FALSE;
    if (!g_shell_parse_argv(str_empty(viewer_cmd) ? "svgviewer" : viewer_cmd,
                            &argc, &argv, NULL)) {
        return FALSE;
    }
    if (argc <= 0 || !argv || !argv[0]) goto done;

    base = g_path_get_basename(argv[0]);
    if (g_strrstr(base, "Msvgviewer") || g_strrstr(base, "Msvgviewer.py")) {
        wants = TRUE;
    }

done:
    g_free(base);
    g_strfreev(argv);
    return wants;
}

static gchar *now_string(void)
{
    GDateTime *dt = g_date_time_new_now_local();
    gchar *s = g_date_time_format(dt, "%Y-%m-%d %H:%M:%S");
    g_date_time_unref(dt);
    return s;
}

static gchar *timestamp_name(void)
{
    GDateTime *dt = g_date_time_new_now_local();
    gchar *s = g_date_time_format(dt, "%Y%m%d_%H%M%S");
    g_date_time_unref(dt);
    return s;
}

static gboolean text_contains_ci(const gchar *s, const gchar *needle)
{
    if (!s || !needle) return FALSE;

    gchar *ls = g_utf8_strdown(s, -1);
    gchar *ln = g_utf8_strdown(needle, -1);
    gboolean ok = (NULL != g_strstr_len(ls, -1, ln));

    g_free(ls);
    g_free(ln);

    return ok;
}

static gboolean is_error_text(const gchar *msg)
{
    const gchar *keys[] = {
        "error", "failed", "fail", "exception", "not found",
        "address already in use", "denied", "refused", "timeout",
        "bad token", "bad magic", "disconnected and retry limit", NULL
    };
    int i;

    for (i = 0; keys[i]; i++) {
        if (text_contains_ci(msg, keys[i])) return TRUE;
    }

    return FALSE;
}

static gboolean is_success_text(const gchar *msg)
{
    const gchar *keys[] = {
        "success", "established successfully", "listening on",
        "received svg", "saved svg", "sent ", NULL
    };
    int i;

    if (is_error_text(msg)) return FALSE;

    for (i = 0; keys[i]; i++) {
        if (text_contains_ci(msg, keys[i])) return TRUE;
    }

    return FALSE;
}

static gboolean is_auth_failure_text(const gchar *msg)
{
    const gchar *keys[] = {
        "permission denied",
        "too many authentication failures",
        "authentication failed",
        "no more authentication methods",
        "host key verification failed",
        NULL
    };
    int i;

    for (i = 0; keys[i]; i++) {
        if (text_contains_ci(msg, keys[i])) return TRUE;
    }

    return FALSE;
}

static gchar *format_log_line(const gchar *msg)
{
    gchar *t = now_string();
    gchar *line = g_strdup_printf("[%s] %s", t, msg ? msg : "");
    g_free(t);
    return line;
}

static gchar *safe_name(const gchar *s)
{
    GString *out = g_string_new("");
    const gchar *p;

    if (str_empty(s)) s = "figure";

    for (p = s; *p; p++) {
        gunichar c = g_utf8_get_char(p);

        if (g_ascii_isalnum(*p) || *p == '_' || *p == '-' || *p == '.') {
            g_string_append_c(out, *p);
        } else if (*p == ' ') {
            g_string_append_c(out, '_');
        } else if ((guchar)*p < 128) {
            g_string_append_c(out, '_');
        }

        if (out->len >= 80) break;

        if (c > 127) {
            /* Keep implementation simple and portable: non-ASCII chars become '_' */
            g_string_append_c(out, '_');
            while (*(p + 1) && !g_utf8_validate(p + 1, 1, NULL)) p++;
        }
    }

    if (out->len == 0) {
        g_string_append(out, "figure");
    }

    return g_string_free(out, FALSE);
}

static gchar *expand_home_path(const gchar *path)
{
    const gchar *home;

    if (str_empty(path)) return g_strdup("");

    if (g_str_has_prefix(path, "~/")) {
        home = g_get_home_dir();
        if (!str_empty(home)) {
            return g_build_filename(home, path + 2, NULL);
        }
    }

    return g_strdup(path);
}

static gchar *resolve_executable_path(const gchar *argv0)
{
    gchar *cwd;
    gchar *path;

    if (str_empty(argv0)) {
        return g_find_program_in_path("rsfclient");
    }

    if (g_path_is_absolute(argv0)) {
        return g_strdup(argv0);
    }

    if (strchr(argv0, G_DIR_SEPARATOR)) {
        cwd = g_get_current_dir();
        path = g_build_filename(cwd, argv0, NULL);
        g_free(cwd);
        return path;
    }

    return g_find_program_in_path(argv0);
}

#ifdef G_OS_WIN32
static void setup_windows_bundle_env(const gchar *exe_path)
{
    gchar *dir = NULL;
    const gchar *old_path;
    gchar *new_path;
    gchar *share;
    gchar *schemas;
    gchar *loaders;
    gchar *loader_cache;

    if (!str_empty(exe_path)) {
        dir = g_path_get_dirname(exe_path);
    }
    if (str_empty(dir)) {
        g_free(dir);
        dir = g_get_current_dir();
    }

    old_path = g_getenv("PATH");
    new_path = g_strconcat(dir, G_SEARCHPATH_SEPARATOR_S,
                           old_path ? old_path : "", NULL);
    share = g_build_filename(dir, "share", NULL);
    schemas = g_build_filename(dir, "share", "glib-2.0", "schemas", NULL);
    loaders = g_build_filename(dir, "lib", "gdk-pixbuf-2.0", "2.10.0", "loaders", NULL);
    loader_cache = g_build_filename(dir, "lib", "gdk-pixbuf-2.0", "2.10.0", "loaders.cache", NULL);

    g_setenv("PATH", new_path, TRUE);
    g_setenv("XDG_DATA_DIRS", share, TRUE);
    g_setenv("GSETTINGS_SCHEMA_DIR", schemas, TRUE);
    g_setenv("GTK_DATA_PREFIX", dir, TRUE);
    g_setenv("GTK_EXE_PREFIX", dir, TRUE);
    g_setenv("GTK_PATH", dir, TRUE);
    g_setenv("GDK_PIXBUF_MODULEDIR", loaders, TRUE);
    if (!g_getenv("GSK_RENDERER")) {
        g_setenv("GSK_RENDERER", "cairo", TRUE);
    }
    if (g_file_test(loader_cache, G_FILE_TEST_EXISTS)) {
        g_setenv("GDK_PIXBUF_MODULE_FILE", loader_cache, TRUE);
    }

    g_free(loader_cache);
    g_free(loaders);
    g_free(schemas);
    g_free(share);
    g_free(new_path);
    g_free(dir);
}
#endif

static gchar *default_ssh_command(void)
{
    gchar *path = g_find_program_in_path("ssh");

    if (!str_empty(path)) {
        return path;
    }

    g_free(path);
    return g_strdup("ssh");
}

static gchar *make_askpass_cancel_path(void)
{
    gchar *uuid = g_uuid_string_random();
    gchar *name = g_strdup_printf("rsfclient-askpass-%s.cancel", uuid);
    gchar *path = g_build_filename(g_get_tmp_dir(), name, NULL);

    g_free(uuid);
    g_free(name);

    return path;
}

static gchar *default_config_path(void)
{
    return g_build_filename(g_get_home_dir(), ".rsfpy_config_gtk", NULL);
}

static void ensure_dir_for_file(const gchar *path)
{
    gchar *dir = g_path_get_dirname(path);
    if (dir) {
        g_mkdir_with_parents(dir, 0700);
        g_free(dir);
    }
}

static gchar *sha256_hex_with_salt(const gchar *salt, const gchar *token)
{
    gchar *data;
    GChecksum *cs;
    gchar *hex;

    data = g_strdup_printf("%s\n%s", salt ? salt : "", token ? token : "");
    cs = g_checksum_new(G_CHECKSUM_SHA256);
    g_checksum_update(cs, (const guchar *)data, strlen(data));
    hex = g_strdup(g_checksum_get_string(cs));

    g_checksum_free(cs);
    g_free(data);

    return hex;
}

static gchar *make_salt(void)
{
    gchar *uuid = g_uuid_string_random();
    return uuid;
}

static gboolean verify_token(Profile *p, const gchar *token)
{
    gchar *got;
    gboolean ok;

    if (!p || str_empty(p->token_hash)) {
        return TRUE;
    }

    got = sha256_hex_with_salt(p->token_salt, token ? token : "");
    ok = g_strcmp0(got, p->token_hash) == 0;
    g_free(got);

    return ok;
}

/* ---------------------------------------------------------------------- */
/* Profile                                                                 */
/* ---------------------------------------------------------------------- */

static Profile *profile_new_default(void)
{
    Profile *p = g_new0(Profile, 1);

    p->name = g_strdup("default");
    p->ssh_cmd = default_ssh_command();
    p->user = g_strdup("");
    p->host = g_strdup("");
    p->ssh_port = 22;
    p->auth_method = g_strdup("default");
    p->password = g_strdup("");
    p->identity_file = g_strdup("");

    p->local_host = g_strdup("127.0.0.1");
    p->local_port = 0;

    p->remote_bind_host = g_strdup("127.0.0.1");
    p->remote_port = 0;

    p->viewer_cmd = g_strdup("svgviewer");
    p->backend = g_strdup("gtk");
    p->save_dir = g_strdup("");
    p->cleanup_temp_files = TRUE;
    p->extra_ssh_args = g_strdup("");

    p->token_salt = g_strdup("");
    p->token_hash = g_strdup("");

    return p;
}

static Profile *profile_copy(Profile *src)
{
    Profile *p = profile_new_default();

    if (!src) return p;

#define CP_STR(field) do { g_free(p->field); p->field = strdup0(src->field); } while (0)

    CP_STR(name);
    CP_STR(ssh_cmd);
    CP_STR(user);
    CP_STR(host);
    p->ssh_port = src->ssh_port;
    CP_STR(auth_method);
    CP_STR(password);
    CP_STR(identity_file);

    CP_STR(local_host);
    p->local_port = src->local_port;

    CP_STR(remote_bind_host);
    p->remote_port = src->remote_port;

    CP_STR(viewer_cmd);
    CP_STR(backend);
    CP_STR(save_dir);
    p->cleanup_temp_files = src->cleanup_temp_files;
    CP_STR(extra_ssh_args);

    CP_STR(token_salt);
    CP_STR(token_hash);

#undef CP_STR

    return p;
}

static void profile_free(Profile *p)
{
    if (!p) return;

#define FREE_FIELD(field) g_free(p->field)

    FREE_FIELD(name);
    FREE_FIELD(ssh_cmd);
    FREE_FIELD(user);
    FREE_FIELD(host);
    FREE_FIELD(auth_method);
    FREE_FIELD(password);
    FREE_FIELD(identity_file);
    FREE_FIELD(local_host);
    FREE_FIELD(remote_bind_host);
    FREE_FIELD(viewer_cmd);
    FREE_FIELD(backend);
    FREE_FIELD(save_dir);
    FREE_FIELD(extra_ssh_args);
    FREE_FIELD(token_salt);
    FREE_FIELD(token_hash);

#undef FREE_FIELD

    g_free(p);
}

static gchar *profile_summary(Profile *p)
{
    if (!p) return g_strdup("<null>");

    if (!str_empty(p->user)) {
        return g_strdup_printf(
            "%s  [%s@%s:%d  R:%d -> L:%d  %s]",
            p->name ? p->name : "unnamed",
            p->user,
            p->host ? p->host : "",
            p->ssh_port,
            p->remote_port,
            p->local_port,
            p->auth_method ? p->auth_method : "default"
        );
    }

    return g_strdup_printf(
        "%s  [%s:%d  R:%d -> L:%d  %s]",
        p->name ? p->name : "unnamed",
        p->host ? p->host : "",
        p->ssh_port,
        p->remote_port,
        p->local_port,
        p->auth_method ? p->auth_method : "default"
    );
}

static gchar *profile_group_name(const gchar *name)
{
    return g_strdup_printf("profile:%s", name ? name : "default");
}

static gchar *key_get_string_default(GKeyFile *kf, const gchar *group,
                                     const gchar *key, const gchar *def)
{
    GError *err = NULL;
    gchar *v = g_key_file_get_string(kf, group, key, &err);

    if (err) {
        g_error_free(err);
        return g_strdup(def ? def : "");
    }

    return v ? v : g_strdup(def ? def : "");
}

static gint key_get_int_default(GKeyFile *kf, const gchar *group,
                                const gchar *key, gint def)
{
    GError *err = NULL;
    gint v = g_key_file_get_integer(kf, group, key, &err);

    if (err) {
        g_error_free(err);
        return def;
    }

    return v;
}

static gboolean key_get_bool_default(GKeyFile *kf, const gchar *group,
                                     const gchar *key, gboolean def)
{
    GError *err = NULL;
    gboolean v = g_key_file_get_boolean(kf, group, key, &err);

    if (err) {
        g_error_free(err);
        return def;
    }

    return v;
}

static gchar *password_codec_key(void)
{
    const gchar *user = g_get_user_name();
    const gchar *home = g_get_home_dir();
    gchar *seed = g_strdup_printf("rsfclient-profile-password-v1|%s|%s",
                                  user ? user : "",
                                  home ? home : "");
    gchar *key = g_compute_checksum_for_string(G_CHECKSUM_SHA256, seed, -1);
    g_free(seed);
    return key;
}

static void xor_password_bytes(guchar *data, gsize len, const gchar *key)
{
    gsize key_len = key ? strlen(key) : 0;
    if (!data || len == 0 || key_len == 0) return;

    for (gsize i = 0; i < len; i++) {
        data[i] = (guchar)(data[i] ^ (guchar)key[i % key_len] ^ 0xA5);
    }
}

static gchar *profile_password_encrypt(const gchar *plain)
{
    gchar *key;
    guchar *buf;
    gchar *b64;
    gchar *out;
    gsize len;

    if (str_empty(plain)) return g_strdup("");

    key = password_codec_key();
    len = strlen(plain);
    buf = (guchar *)g_memdup2(plain, len);
    xor_password_bytes(buf, len, key);
    b64 = g_base64_encode(buf, len);
    out = g_strconcat("enc:v1:", b64, NULL);

    g_free(b64);
    g_free(buf);
    g_free(key);
    return out;
}

static gchar *profile_password_decrypt(const gchar *stored)
{
    gchar *key;
    guchar *buf;
    gsize len = 0;
    gchar *out;

    if (str_empty(stored)) return g_strdup("");
    if (!g_str_has_prefix(stored, "enc:v1:")) {
        return g_strdup(stored);
    }

    key = password_codec_key();
    buf = g_base64_decode(stored + strlen("enc:v1:"), &len);
    if (!buf) {
        g_free(key);
        return g_strdup("");
    }

    xor_password_bytes(buf, len, key);
    out = g_strndup((const gchar *)buf, len);
    g_free(buf);
    g_free(key);
    return out;
}

static Profile *profile_load_from_group(GKeyFile *kf, const gchar *group)
{
    Profile *p = profile_new_default();
    const gchar *name = group;
    gchar *ssh_default = default_ssh_command();

    if (g_str_has_prefix(group, "profile:")) {
        name = group + strlen("profile:");
    }

#define SET_STR(field, key, def) do { \
        g_free(p->field); \
        p->field = key_get_string_default(kf, group, key, def); \
    } while (0)

    g_free(p->name);
    p->name = g_strdup(name);

    SET_STR(ssh_cmd, "ssh_cmd", ssh_default);
    SET_STR(user, "user", "");
    SET_STR(host, "host", "");
    p->ssh_port = key_get_int_default(kf, group, "ssh_port", 22);
    SET_STR(auth_method, "auth_method", "default");
    {
        gchar *stored_password = key_get_string_default(kf, group, "password", "");
        g_free(p->password);
        p->password = profile_password_decrypt(stored_password);
        g_free(stored_password);
    }
    SET_STR(identity_file, "identity_file", "");

    SET_STR(local_host, "local_host", "127.0.0.1");
    p->local_port = key_get_int_default(kf, group, "local_port", 0);

    SET_STR(remote_bind_host, "remote_bind_host", "127.0.0.1");
    p->remote_port = key_get_int_default(kf, group, "remote_port", 0);

    SET_STR(viewer_cmd, "viewer_cmd", "svgviewer");
    SET_STR(backend, "backend", "gtk");
    SET_STR(save_dir, "save_dir", "");
    p->cleanup_temp_files = key_get_bool_default(kf, group, "cleanup_temp_files", TRUE);
    SET_STR(extra_ssh_args, "extra_ssh_args", "");

    SET_STR(token_salt, "token_salt", "");
    SET_STR(token_hash, "token_hash", "");

#undef SET_STR

    g_free(ssh_default);

    return p;
}

static void profile_save_to_keyfile(GKeyFile *kf, Profile *p)
{
    gchar *group;

    if (!kf || !p || str_empty(p->name)) return;

    group = profile_group_name(p->name);

#define SET_STR(key, val) g_key_file_set_string(kf, group, key, val ? val : "")

    SET_STR("ssh_cmd", p->ssh_cmd);
    SET_STR("user", p->user);
    SET_STR("host", p->host);
    g_key_file_set_integer(kf, group, "ssh_port", p->ssh_port);
    SET_STR("auth_method", p->auth_method);
    {
        gchar *stored_password = profile_password_encrypt(p->password);
        SET_STR("password", stored_password);
        g_free(stored_password);
    }
    SET_STR("identity_file", p->identity_file);

    SET_STR("local_host", p->local_host);
    g_key_file_set_integer(kf, group, "local_port", p->local_port);

    SET_STR("remote_bind_host", p->remote_bind_host);
    g_key_file_set_integer(kf, group, "remote_port", p->remote_port);

    SET_STR("viewer_cmd", p->viewer_cmd);
    SET_STR("backend", p->backend);
    SET_STR("save_dir", p->save_dir);
    g_key_file_set_boolean(kf, group, "cleanup_temp_files", p->cleanup_temp_files);
    SET_STR("extra_ssh_args", p->extra_ssh_args);

    SET_STR("token_salt", p->token_salt);
    SET_STR("token_hash", p->token_hash);

#undef SET_STR

    g_key_file_set_string(kf, "general", "last_profile", p->name);

    g_free(group);
}

static void app_load_config(AppState *app)
{
    GError *err = NULL;

    app->config_path = default_config_path();
    app->config = g_key_file_new();

    if (g_file_test(app->config_path, G_FILE_TEST_EXISTS)) {
        if (!g_key_file_load_from_file(app->config, app->config_path, G_KEY_FILE_KEEP_COMMENTS, &err)) {
            if (err) g_error_free(err);
        }
    }
}

static gboolean app_save_config(AppState *app)
{
    gchar *data;
    gsize len;
    GError *err = NULL;
    gboolean ok;

    if (!app || !app->config || !app->config_path) return FALSE;

    data = g_key_file_to_data(app->config, &len, NULL);
    ensure_dir_for_file(app->config_path);
    ok = g_file_set_contents(app->config_path, data, len, &err);

    if (err) {
        g_error_free(err);
    }

    g_free(data);

    return ok;
}

static void app_reload_profiles(AppState *app)
{
    gchar **groups;
    gsize ngroups = 0;
    gsize i;

    if (!app->profiles) {
        app->profiles = g_ptr_array_new_with_free_func((GDestroyNotify)profile_free);
    } else {
        g_ptr_array_set_size(app->profiles, 0);
    }

    groups = g_key_file_get_groups(app->config, &ngroups);

    for (i = 0; i < ngroups; i++) {
        if (g_str_has_prefix(groups[i], "profile:")) {
            Profile *p = profile_load_from_group(app->config, groups[i]);
            g_ptr_array_add(app->profiles, p);
        }
    }

    g_strfreev(groups);
}

static Profile *app_get_profile_by_index(AppState *app, guint idx)
{
    if (!app || !app->profiles || idx >= app->profiles->len) return NULL;
    return g_ptr_array_index(app->profiles, idx);
}

/* ---------------------------------------------------------------------- */
/* Logging                                                                 */
/* ---------------------------------------------------------------------- */

typedef struct {
    AppState *app;
    gchar *line;
    gchar *level;
} LogItem;

static gboolean log_idle_cb(gpointer data)
{
    LogItem *it = (LogItem *)data;
    GtkTextIter end;

    if (it->app && it->app->log_buffer) {
        gtk_text_buffer_get_end_iter(it->app->log_buffer, &end);
        gtk_text_buffer_insert_with_tags_by_name(
            it->app->log_buffer,
            &end,
            it->line,
            -1,
            it->level ? it->level : "info",
            NULL
        );
        gtk_text_buffer_insert(it->app->log_buffer, &end, "\n", -1);

        if (it->app->log_view) {
            GtkTextMark *mark = gtk_text_buffer_get_insert(it->app->log_buffer);
            gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(it->app->log_view), mark);
        }
    } else {
        if (g_strcmp0(it->level, "error") == 0) {
            g_printerr("\033[1;31m%s\033[0m\n", it->line);
        } else if (g_strcmp0(it->level, "success") == 0) {
            g_printerr("\033[1;32m%s\033[0m\n", it->line);
        } else if (g_strcmp0(it->level, "warning") == 0) {
            g_printerr("\033[1;33m%s\033[0m\n", it->line);
        } else {
            g_printerr("%s\n", it->line);
        }
    }

    g_free(it->line);
    g_free(it->level);
    g_free(it);

    return G_SOURCE_REMOVE;
}

static void app_log(AppState *app, const gchar *msg, const gchar *level)
{
    LogItem *it = g_new0(LogItem, 1);
    gchar *line = format_log_line(msg);

    if (!level) {
        if (is_error_text(msg)) level = "error";
        else if (is_success_text(msg)) level = "success";
        else level = "info";
    }

    it->app = app;
    it->line = line;
    it->level = g_strdup(level);

    g_main_context_invoke(NULL, log_idle_cb, it);
}

/* ---------------------------------------------------------------------- */
/* Sender mode                                                             */
/* ---------------------------------------------------------------------- */

static gboolean read_stdin_all(GByteArray **out, GError **err)
{
    GByteArray *arr = g_byte_array_new();
    guint8 buf[8192];

    while (!feof(stdin)) {
        size_t n = fread(buf, 1, sizeof(buf), stdin);
        if (n > 0) {
            g_byte_array_append(arr, buf, (guint)n);
        }

        if (ferror(stdin)) {
            g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED, "failed to read stdin");
            g_byte_array_unref(arr);
            return FALSE;
        }
    }

    *out = arr;
    return TRUE;
}

static gchar *json_escape(const gchar *s)
{
    GString *out = g_string_new("");
    const gchar *p;

    if (!s) return g_strdup("");

    for (p = s; *p; p++) {
        switch (*p) {
            case '\\': g_string_append(out, "\\\\"); break;
            case '"': g_string_append(out, "\\\""); break;
            case '\n': g_string_append(out, "\\n"); break;
            case '\r': g_string_append(out, "\\r"); break;
            case '\t': g_string_append(out, "\\t"); break;
            default: g_string_append_c(out, *p); break;
        }
    }

    return g_string_free(out, FALSE);
}

static gboolean output_write_all(GOutputStream *out, const void *buf, gsize len, GError **err)
{
    gsize done = 0;
    return g_output_stream_write_all(out, buf, len, &done, NULL, err);
}

static gboolean send_payload(const gchar *host, gint port, const gchar *token,
                             const guint8 *payload, gsize payload_len,
                             const gchar *title, GError **err)
{
    GSocketClient *client;
    GSocketConnection *conn;
    GOutputStream *out;
    gchar *escaped_title;
    gchar *time_s;
    gchar *meta;
    guint16 token_len_be;
    guint32 meta_len_be;
    guint64 payload_len_be;
    gsize token_len;
    gsize meta_len;
    gboolean ok = FALSE;

    client = g_socket_client_new();
    conn = g_socket_client_connect_to_host(client, host, port, NULL, err);
    g_object_unref(client);

    if (!conn) return FALSE;

    out = g_io_stream_get_output_stream(G_IO_STREAM(conn));

    escaped_title = json_escape(title ? title : "");
    time_s = now_string();
    meta = g_strdup_printf("{\"title\":\"%s\",\"time\":\"%s\"}", escaped_title, time_s);

    token_len = strlen(token ? token : "");
    meta_len = strlen(meta);

    if (token_len > 65535) {
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED, "token is too long");
        goto done;
    }

    token_len_be = GUINT16_TO_BE((guint16)token_len);
    meta_len_be = GUINT32_TO_BE((guint32)meta_len);
    payload_len_be = GUINT64_TO_BE((guint64)payload_len);

    if (!output_write_all(out, MAGIC, MAGIC_LEN, err)) goto done;
    if (!output_write_all(out, &token_len_be, 2, err)) goto done;
    if (token_len > 0 && !output_write_all(out, token, token_len, err)) goto done;
    if (!output_write_all(out, &meta_len_be, 4, err)) goto done;
    if (meta_len > 0 && !output_write_all(out, meta, meta_len, err)) goto done;
    if (!output_write_all(out, &payload_len_be, 8, err)) goto done;
    if (payload_len > 0 && !output_write_all(out, payload, payload_len, err)) goto done;

    ok = TRUE;

done:
    g_free(escaped_title);
    g_free(time_s);
    g_free(meta);
    g_object_unref(conn);

    return ok;
}

static int sender_main(int argc, char **argv)
{
    const gchar *host = g_getenv("RSFCLIENT_SEND_HOST");
    const gchar *token = g_getenv("RSFCLIENT_SEND_TOKEN");
    const gchar *title = "";
    const gchar *port_env = g_getenv("RSFCLIENT_SEND_PORT");
    gint port = port_env ? atoi(port_env) : DEFAULT_PORT;
    GByteArray *payload = NULL;
    GError *err = NULL;
    int i;

    if (str_empty(host)) host = g_getenv("RSFVIEW_HOST");
    if (str_empty(host)) host = "127.0.0.1";
    if (!port_env) {
        const gchar *legacy_port = g_getenv("RSFVIEW_PORT");
        if (!str_empty(legacy_port)) port = atoi(legacy_port);
    }
    if (str_empty(token)) token = g_getenv("RSFVIEW_TOKEN");
    if (!token) token = "";

    for (i = 1; i < argc; i++) {
        if (g_strcmp0(argv[i], "--send") == 0) {
            continue;
        } else if (g_strcmp0(argv[i], "--host") == 0 && i + 1 < argc) {
            host = argv[++i];
        } else if (g_strcmp0(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (g_strcmp0(argv[i], "--token") == 0 && i + 1 < argc) {
            token = argv[++i];
        } else if (g_strcmp0(argv[i], "--title") == 0 && i + 1 < argc) {
            title = argv[++i];
        } else if (g_strcmp0(argv[i], "--help") == 0 || g_strcmp0(argv[i], "-h") == 0) {
            g_print("Usage: rsfclient --send [--host 127.0.0.1] [--port PORT] [--token TOKEN] [--title TITLE] < figure.svg\n");
            return 0;
        }
    }

    if (!read_stdin_all(&payload, &err)) {
        g_printerr("\033[1;31merror: %s\033[0m\n", err ? err->message : "read stdin failed");
        if (err) g_error_free(err);
        return 1;
    }

    if (payload->len == 0) {
        g_printerr("\033[1;31merror: empty stdin\033[0m\n");
        g_byte_array_unref(payload);
        return 1;
    }

    if (!send_payload(host, port, token, payload->data, payload->len, title, &err)) {
        g_printerr("\033[1;31mfailed to send SVG: %s\033[0m\n", err ? err->message : "unknown error");
        if (err) g_error_free(err);
        g_byte_array_unref(payload);
        return 1;
    }

    g_printerr("\033[1;32msent %u bytes\033[0m\n", payload->len);
    g_byte_array_unref(payload);

    return 0;
}

/* ---------------------------------------------------------------------- */
/* SSH askpass mode                                                        */
/* ---------------------------------------------------------------------- */

typedef struct {
    GtkApplication *app;
    gchar *prompt;
    gchar *cancel_path;
    gchar *answer;
    guint cancel_source;
    int status;
} AskpassState;

static gboolean askpass_cancel_poll(gpointer data)
{
    AskpassState *state = (AskpassState *)data;

    if (!state || str_empty(state->cancel_path)) return G_SOURCE_CONTINUE;

    if (g_file_test(state->cancel_path, G_FILE_TEST_EXISTS)) {
        state->status = 1;
        state->cancel_source = 0;
        g_application_quit(G_APPLICATION(state->app));
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

static void on_askpass_response(GtkButton *button, gpointer data)
{
    AskpassState *state = (AskpassState *)data;
    GtkWidget *entry = g_object_get_data(G_OBJECT(button), "password-entry");

    if (entry) {
        g_free(state->answer);
        state->answer = g_strdup(gtk_editable_get_text(GTK_EDITABLE(entry)));
        state->status = 0;
    }

    g_application_quit(G_APPLICATION(state->app));
}

static void on_askpass_cancel(GtkButton *button, gpointer data)
{
    AskpassState *state = (AskpassState *)data;
    (void)button;

    state->status = 1;
    g_application_quit(G_APPLICATION(state->app));
}

static gboolean on_askpass_close(GtkWindow *window, gpointer data)
{
    AskpassState *state = (AskpassState *)data;
    (void)window;

    state->status = 1;
    g_application_quit(G_APPLICATION(state->app));

    return TRUE;
}

static void askpass_activate(GtkApplication *gtk_app, gpointer user_data)
{
    AskpassState *state = (AskpassState *)user_data;
    GtkWidget *window;
    GtkWidget *box;
    GtkWidget *label;
    GtkWidget *entry;
    GtkWidget *buttons;
    GtkWidget *ok;
    GtkWidget *cancel;

    state->app = gtk_app;

    window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(GTK_WINDOW(window), "RSF Client Authentication");
    gtk_window_set_default_size(GTK_WINDOW(window), 420, 150);
    g_signal_connect(window, "close-request", G_CALLBACK(on_askpass_close), state);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_window_set_child(GTK_WINDOW(window), box);

    label = gtk_label_new(str_empty(state->prompt) ? "SSH authentication required" : state->prompt);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), label);

    entry = gtk_password_entry_new();
    gtk_box_append(GTK_BOX(box), entry);

    buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);

    cancel = gtk_button_new_with_label("Cancel");
    ok = gtk_button_new_with_label("OK");
    g_object_set_data(G_OBJECT(ok), "password-entry", entry);
    g_signal_connect(cancel, "clicked", G_CALLBACK(on_askpass_cancel), state);
    g_signal_connect(ok, "clicked", G_CALLBACK(on_askpass_response), state);

    gtk_box_append(GTK_BOX(buttons), cancel);
    gtk_box_append(GTK_BOX(buttons), ok);
    gtk_box_append(GTK_BOX(box), buttons);

    if (!str_empty(state->cancel_path)) {
        state->cancel_source = g_timeout_add(200, askpass_cancel_poll, state);
    }

    gtk_window_present(GTK_WINDOW(window));
}

static int askpass_main(int argc, char **argv)
{
    const gchar *saved_password = g_getenv("RSFCLIENT_ASKPASS_PASSWORD");
    char *askpass_argv[] = {"rsfclient-askpass", NULL};
    AskpassState state = {0};
    int status;

    if (!str_empty(saved_password)) {
        g_print("%s\n", saved_password);
        return 0;
    }

    state.prompt = g_strdup(argc > 1 ? argv[1] : "SSH authentication required");
    state.cancel_path = g_strdup(g_getenv("RSFCLIENT_ASKPASS_CANCEL_FILE"));
    state.status = 1;

    state.app = gtk_application_new("org.rsfpy.rsfclient.askpass", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(state.app, "activate", G_CALLBACK(askpass_activate), &state);

    status = g_application_run(G_APPLICATION(state.app), 1, askpass_argv);
    if (state.cancel_source) {
        g_source_remove(state.cancel_source);
    }
    g_object_unref(state.app);

    if (status == 0 && state.status == 0 && !str_empty(state.answer)) {
        g_print("%s\n", state.answer);
    } else {
        state.status = 1;
    }

    g_free(state.prompt);
    g_free(state.cancel_path);
    g_free(state.answer);

    return state.status;
}

/* ---------------------------------------------------------------------- */
/* Receiver protocol                                                       */
/* ---------------------------------------------------------------------- */

static gboolean input_read_exact(GInputStream *in, void *buf, gsize len, GError **err)
{
    gsize done = 0;
    return g_input_stream_read_all(in, buf, len, &done, NULL, err);
}

static gchar *extract_string_from_meta(const gchar *meta, const gchar *key, const gchar *fallback)
{
    const gchar *p;
    const gchar *colon;
    const gchar *q;
    GString *out;

    if (str_empty(meta) || str_empty(key)) return g_strdup(fallback ? fallback : "");

    {
        gchar *needle = g_strdup_printf("\"%s\"", key);
        p = g_strstr_len(meta, -1, needle);
        g_free(needle);
    }
    if (!p) return g_strdup(fallback ? fallback : "");

    colon = strchr(p, ':');
    if (!colon) return g_strdup(fallback ? fallback : "");

    q = strchr(colon, '"');
    if (!q) return g_strdup(fallback ? fallback : "");
    q++;

    out = g_string_new("");

    while (*q) {
        if (*q == '"') break;

        if (*q == '\\' && *(q + 1)) {
            q++;
            switch (*q) {
                case 'n': g_string_append_c(out, '\n'); break;
                case 'r': g_string_append_c(out, '\r'); break;
                case 't': g_string_append_c(out, '\t'); break;
                default: g_string_append_c(out, *q); break;
            }
        } else {
            g_string_append_c(out, *q);
        }

        q++;
    }

    if (out->len == 0) {
        g_string_append(out, fallback ? fallback : "");
    }

    return g_string_free(out, FALSE);
}

static gchar *extract_title_from_meta(const gchar *meta)
{
    return extract_string_from_meta(meta, "title", "figure");
}

typedef struct {
    Runtime *rt;
    gchar *path;
    gboolean temporary;
} OpenJob;

static gboolean open_viewer_idle(gpointer data);

static gchar *make_output_path(Profile *p, const gchar *title, gboolean *temporary)
{
    gchar *safe = safe_name(title);
    gchar *stamp = timestamp_name();
    gchar *uuid = g_uuid_string_random();
    gchar *short_uuid = g_strndup(uuid, 8);
    gchar *filename = g_strdup_printf("%s_%s_%s.svg", safe, stamp, short_uuid);
    gchar *base = NULL;
    gchar *path;

    base = g_strdup((p && !str_empty(p->save_dir)) ? p->save_dir : g_get_tmp_dir());
    *temporary = !p || p->cleanup_temp_files;

    g_mkdir_with_parents(base, 0700);
    path = g_build_filename(base, filename, NULL);

    g_free(safe);
    g_free(stamp);
    g_free(uuid);
    g_free(short_uuid);
    g_free(filename);
    g_free(base);

    return path;
}

static gboolean stream_payload_to_file(GInputStream *in, FILE *fp, guint64 size, GError **err)
{
    guint8 buf[8192];
    guint64 left = size;

    while (left > 0) {
        gsize want = (left > sizeof(buf)) ? sizeof(buf) : (gsize)left;
        gssize n = g_input_stream_read(in, buf, want, NULL, err);

        if (n < 0) return FALSE;
        if (n == 0) {
            g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED, "connection closed before payload finished");
            return FALSE;
        }

        if (fwrite(buf, 1, (size_t)n, fp) != (size_t)n) {
            g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED, "failed to write output SVG");
            return FALSE;
        }

        left -= (guint64)n;
    }

    return TRUE;
}

static void handle_connection(Runtime *rt, GSocketConnection *conn)
{
    GInputStream *in;
    GError *err = NULL;
    guint8 magic[MAGIC_LEN];
    guint16 token_len_be, token_len;
    guint32 meta_len_be, meta_len;
    guint64 size_be, size;
    gchar *token = NULL;
    gchar *meta = NULL;
    gchar *title = NULL;
    gchar *path = NULL;
    gboolean temporary = TRUE;
    FILE *fp = NULL;

    in = g_io_stream_get_input_stream(G_IO_STREAM(conn));

    if (!input_read_exact(in, magic, MAGIC_LEN, &err)) goto fail;
    if (memcmp(magic, MAGIC, MAGIC_LEN) != 0) {
        g_set_error(&err, G_IO_ERROR, G_IO_ERROR_FAILED, "bad magic");
        goto fail;
    }

    if (!input_read_exact(in, &token_len_be, 2, &err)) goto fail;
    token_len = GUINT16_FROM_BE(token_len_be);

    token = g_malloc0(token_len + 1);
    if (token_len > 0 && !input_read_exact(in, token, token_len, &err)) goto fail;

    if (!verify_token(rt->profile, token)) {
        g_set_error(&err, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "bad token");
        goto fail;
    }

    if (!input_read_exact(in, &meta_len_be, 4, &err)) goto fail;
    meta_len = GUINT32_FROM_BE(meta_len_be);

    if (meta_len > 16 * 1024 * 1024) {
        g_set_error(&err, G_IO_ERROR, G_IO_ERROR_FAILED, "metadata too large");
        goto fail;
    }

    meta = g_malloc0(meta_len + 1);
    if (meta_len > 0 && !input_read_exact(in, meta, meta_len, &err)) goto fail;

    if (!input_read_exact(in, &size_be, 8, &err)) goto fail;
    size = GUINT64_FROM_BE(size_be);

    if (size == 0) {
        g_set_error(&err, G_IO_ERROR, G_IO_ERROR_FAILED, "empty SVG payload");
        goto fail;
    }

    if (size > MAX_PAYLOAD) {
        g_set_error(&err, G_IO_ERROR, G_IO_ERROR_FAILED, "payload too large");
        goto fail;
    }

    title = extract_title_from_meta(meta);
    path = make_output_path(rt->profile, title, &temporary);

    fp = g_fopen(path, "wb");
    if (!fp) {
        g_set_error(&err, G_IO_ERROR, G_IO_ERROR_FAILED, "failed to open output SVG: %s", path);
        goto fail;
    }

    if (!stream_payload_to_file(in, fp, size, &err)) goto fail;

    fclose(fp);
    fp = NULL;

    {
        gchar *msg = g_strdup_printf("received SVG payload: %" G_GUINT64_FORMAT " bytes", size);
        app_log(rt->app, msg, "success");
        g_free(msg);
    }

    {
        gchar *msg = g_strdup_printf("saved SVG: %s", path);
        app_log(rt->app, msg, "success");
        g_free(msg);
    }

    {
        OpenJob *job = g_new0(OpenJob, 1);
        job->rt = rt;
        job->path = g_strdup(path);
        job->temporary = temporary;
        g_main_context_invoke(NULL, open_viewer_idle, job);
    }

    goto done;

fail:
    app_log(rt->app, err ? err->message : "connection error", "error");

done:
    if (fp) fclose(fp);
    if (err) g_error_free(err);
    g_free(token);
    g_free(meta);
    g_free(title);
    g_free(path);
}

static gpointer connection_thread(gpointer data)
{
    GPtrArray *arr = (GPtrArray *)data;
    Runtime *rt = g_ptr_array_index(arr, 0);
    GSocketConnection *conn = g_ptr_array_index(arr, 1);

    handle_connection(rt, conn);

    g_object_unref(conn);
    g_ptr_array_free(arr, TRUE);

    return NULL;
}

/* ---------------------------------------------------------------------- */
/* Viewer spawn                                                            */
/* ---------------------------------------------------------------------- */

typedef struct {
    AppState *app;
    gchar *path;
} ChildDeleteData;

typedef struct {
    AppState *app;
    gint fd;
    gchar *stream_name;
} ViewerOutputData;

static void remove_temporary_svg(AppState *app, const gchar *path)
{
    gchar *msg;

    if (str_empty(path)) return;

    if (g_remove(path) == 0) {
        msg = g_strdup_printf("removed temporary SVG: %s", path);
        app_log(app, msg, "info");
    } else {
        msg = g_strdup_printf("failed to remove temporary SVG: %s (%s)",
                              path, g_strerror(errno));
        app_log(app, msg, "error");
    }

    g_free(msg);
}

static gpointer viewer_output_thread(gpointer data)
{
    ViewerOutputData *d = (ViewerOutputData *)data;
    FILE *fp;
    char buf[2048];

    if (!d) return NULL;

    fp = fdopen(d->fd, "r");
    if (!fp) {
        g_close(d->fd, NULL);
        g_free(d->stream_name);
        g_free(d);
        return NULL;
    }

    while (fgets(buf, sizeof(buf), fp) != NULL) {
        gchar *line = g_strdup(buf);
        gchar *msg;

        g_strchomp(line);
        if (!str_empty(line)) {
            msg = g_strdup_printf("svgviewer %s: %s",
                                  d->stream_name ? d->stream_name : "output",
                                  line);
            app_log(d->app, msg, "warning");
            g_free(msg);
        }

        g_free(line);
    }

    fclose(fp);
    g_free(d->stream_name);
    g_free(d);

    return NULL;
}

static void watch_viewer_output(AppState *app, gint fd, const gchar *stream_name)
{
    ViewerOutputData *d;

    if (fd < 0) return;

    d = g_new0(ViewerOutputData, 1);
    d->app = app;
    d->fd = fd;
    d->stream_name = g_strdup(stream_name);

    g_thread_unref(g_thread_new("svgviewer-output", viewer_output_thread, d));
}

static void viewer_child_exit_cb(GPid pid, gint status, gpointer user_data)
{
    ChildDeleteData *d = (ChildDeleteData *)user_data;

    if (d && d->path) {
        remove_temporary_svg(d->app, d->path);
        g_free(d->path);
    }

    g_spawn_close_pid(pid);
    g_free(d);
}

static gchar **argv_append_many(gchar **base, const gchar **extra)
{
    GPtrArray *arr = g_ptr_array_new_with_free_func(g_free);
    int i;

    if (base) {
        for (i = 0; base[i]; i++) {
            g_ptr_array_add(arr, g_strdup(base[i]));
        }
    }

    if (extra) {
        for (i = 0; extra[i]; i++) {
            g_ptr_array_add(arr, g_strdup(extra[i]));
        }
    }

    g_ptr_array_add(arr, NULL);
    return (gchar **)g_ptr_array_free(arr, FALSE);
}

static gboolean open_viewer_idle(gpointer data)
{
    OpenJob *job = (OpenJob *)data;
    Profile *p = job->rt->profile;
    gchar **base_argv = NULL;
    gchar **argv = NULL;
    GError *err = NULL;
    GPid pid = 0;
    gint stdout_fd = -1;
    gint stderr_fd = -1;
    GPtrArray *arr;
    gint argc = 0;
    int i;

    if (!g_shell_parse_argv(str_empty(p->viewer_cmd) ? "svgviewer" : p->viewer_cmd,
                            &argc, &base_argv, &err)) {
        app_log(job->rt->app, err ? err->message : "failed to parse viewer command", "error");
        if (err) g_error_free(err);
        goto done;
    }

    arr = g_ptr_array_new_with_free_func(g_free);

    for (i = 0; base_argv[i]; i++) {
        g_ptr_array_add(arr, g_strdup(base_argv[i]));
    }

    if (viewer_command_wants_backend_arg(p->viewer_cmd, p->backend)) {
        g_ptr_array_add(arr, g_strdup("--backend"));
        g_ptr_array_add(arr, g_strdup(p->backend));
    }

    g_ptr_array_add(arr, g_strdup(job->path));
    g_ptr_array_add(arr, NULL);

    argv = (gchar **)g_ptr_array_free(arr, FALSE);

    {
        gchar *cmdline = g_strjoinv(" ", argv);
        gchar *msg = g_strdup_printf("open viewer: %s", cmdline);
        app_log(job->rt->app, msg, "info");
        g_free(msg);
        g_free(cmdline);
    }

    if (!g_spawn_async_with_pipes(NULL, argv, NULL,
                                  G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                  NULL, NULL, &pid,
                                  NULL, &stdout_fd, &stderr_fd, &err)) {
        app_log(job->rt->app, err ? err->message : "failed to open viewer", "error");
        if (job->temporary) {
            remove_temporary_svg(job->rt->app, job->path);
        }
        if (err) g_error_free(err);
        goto done;
    }

    watch_viewer_output(job->rt->app, stdout_fd, "stdout");
    watch_viewer_output(job->rt->app, stderr_fd, "stderr");

    if (job->temporary) {
        ChildDeleteData *d = g_new0(ChildDeleteData, 1);
        d->app = job->rt->app;
        d->path = g_strdup(job->path);
        g_child_watch_add(pid, viewer_child_exit_cb, d);
    } else {
        g_spawn_close_pid(pid);
    }

done:
    if (base_argv) g_strfreev(base_argv);
    if (argv) g_strfreev(argv);
    g_free(job->path);
    g_free(job);

    return G_SOURCE_REMOVE;
}

/* ---------------------------------------------------------------------- */
/* Runtime                                                                 */
/* ---------------------------------------------------------------------- */

static gboolean runtime_is_stopping(Runtime *rt)
{
    gboolean v;
    g_mutex_lock(&rt->lock);
    v = rt->stopping;
    g_mutex_unlock(&rt->lock);
    return v;
}

static void runtime_set_stopping(Runtime *rt, gboolean v)
{
    g_mutex_lock(&rt->lock);
    rt->stopping = v;
    g_mutex_unlock(&rt->lock);
}

static void runtime_clear_auth_failure(Runtime *rt)
{
    g_mutex_lock(&rt->lock);
    rt->auth_failed = FALSE;
    g_free(rt->auth_failure_reason);
    rt->auth_failure_reason = NULL;
    g_mutex_unlock(&rt->lock);
}

static void runtime_mark_auth_failure(Runtime *rt, const gchar *reason)
{
    g_mutex_lock(&rt->lock);
    rt->auth_failed = TRUE;
    g_free(rt->auth_failure_reason);
    rt->auth_failure_reason = g_strdup(reason ? reason : "SSH authentication failed");
    g_mutex_unlock(&rt->lock);
}

static gboolean runtime_get_auth_failure(Runtime *rt, gchar **reason)
{
    gboolean failed;

    g_mutex_lock(&rt->lock);
    failed = rt->auth_failed;
    if (reason) {
        *reason = failed ? g_strdup(rt->auth_failure_reason) : NULL;
    }
    g_mutex_unlock(&rt->lock);

    return failed;
}

static gchar *shell_single_quote(const gchar *s);
static gchar *pair_config_keepalive_command(Runtime *rt);

static gchar **build_ssh_argv(Runtime *rt)
{
    Profile *p = rt->profile;
    GPtrArray *arr = g_ptr_array_new_with_free_func(g_free);
    gchar *target;
    gchar *forward;
    gchar *port_s;
    gchar *remote_cmd;
    gchar *identity_path = NULL;
    gchar **extra = NULL;
    int i;

    g_ptr_array_add(arr, g_strdup(str_empty(p->ssh_cmd) ? "ssh" : p->ssh_cmd));
    g_ptr_array_add(arr, g_strdup("-T"));
    g_ptr_array_add(arr, g_strdup("-p"));

    port_s = g_strdup_printf("%d", p->ssh_port);
    g_ptr_array_add(arr, port_s);

    g_ptr_array_add(arr, g_strdup("-o"));
    g_ptr_array_add(arr, g_strdup("ExitOnForwardFailure=yes"));
    g_ptr_array_add(arr, g_strdup("-o"));
    g_ptr_array_add(arr, g_strdup("ServerAliveInterval=30"));
    g_ptr_array_add(arr, g_strdup("-o"));
    g_ptr_array_add(arr, g_strdup("ServerAliveCountMax=3"));
    g_ptr_array_add(arr, g_strdup("-o"));
    g_ptr_array_add(arr, g_strdup("StrictHostKeyChecking=accept-new"));
    g_ptr_array_add(arr, g_strdup("-o"));
    g_ptr_array_add(arr, g_strdup("NumberOfPasswordPrompts=1"));
    g_ptr_array_add(arr, g_strdup("-o"));
    g_ptr_array_add(arr, g_strdup("ConnectTimeout=10"));

    if (g_strcmp0(p->auth_method, "password") == 0) {
        g_ptr_array_add(arr, g_strdup("-o"));
        g_ptr_array_add(arr, g_strdup("PreferredAuthentications=password,keyboard-interactive"));
        g_ptr_array_add(arr, g_strdup("-o"));
        g_ptr_array_add(arr, g_strdup("PubkeyAuthentication=no"));
    } else if (g_strcmp0(p->auth_method, "identity") == 0 && !str_empty(p->identity_file)) {
        identity_path = expand_home_path(p->identity_file);
        g_ptr_array_add(arr, g_strdup("-i"));
        g_ptr_array_add(arr, identity_path);
        identity_path = NULL;
        g_ptr_array_add(arr, g_strdup("-o"));
        g_ptr_array_add(arr, g_strdup("IdentitiesOnly=yes"));
    }

    g_ptr_array_add(arr, g_strdup("-R"));

    forward = g_strdup_printf("%s:%d:%s:%d",
                              str_empty(p->remote_bind_host) ? "127.0.0.1" : p->remote_bind_host,
                              p->remote_port,
                              str_empty(p->local_host) ? "127.0.0.1" : p->local_host,
                              p->local_port);
    g_ptr_array_add(arr, forward);

    if (!str_empty(p->extra_ssh_args)) {
        GError *err = NULL;
        if (g_shell_parse_argv(p->extra_ssh_args, NULL, &extra, &err)) {
            for (i = 0; extra[i]; i++) {
                g_ptr_array_add(arr, g_strdup(extra[i]));
            }
            g_strfreev(extra);
        } else if (err) {
            g_error_free(err);
        }
    }

    if (!str_empty(p->user)) {
        target = g_strdup_printf("%s@%s", p->user, p->host ? p->host : "");
    } else {
        target = g_strdup(p->host ? p->host : "");
    }

    g_ptr_array_add(arr, target);
    remote_cmd = pair_config_keepalive_command(rt);
    g_ptr_array_add(arr, remote_cmd);
    g_ptr_array_add(arr, NULL);

    g_free(identity_path);
    return (gchar **)g_ptr_array_free(arr, FALSE);
}

static gchar *shell_single_quote(const gchar *s)
{
    GString *out = g_string_new("'");
    const gchar *p;

    for (p = s ? s : ""; *p; p++) {
        if (*p == '\'') {
            g_string_append(out, "'\\''");
        } else {
            g_string_append_c(out, *p);
        }
    }

    g_string_append_c(out, '\'');
    return g_string_free(out, FALSE);
}

static gchar *pair_config_remote_name(Profile *p)
{
    return g_strdup_printf(".rsfclient_server_%s",
                           str_empty(p->user) ? "default" : p->user);
}

static gchar *pair_config_content(Runtime *rt)
{
    return g_strdup_printf(
        "host=127.0.0.1\n"
        "port=%d\n"
        "token=%s\n"
        "user=%s\n",
        rt->profile->remote_port,
        rt->pair_token ? rt->pair_token : "",
        str_empty(rt->profile->user) ? "default" : rt->profile->user);
}

static gchar *pair_config_keepalive_command(Runtime *rt)
{
    gchar *remote_name = pair_config_remote_name(rt->profile);
    gchar *content = pair_config_content(rt);
    gchar *quoted_name = shell_single_quote(remote_name);
    gchar *quoted_content = shell_single_quote(content);
    gchar *cmd;

    cmd = g_strdup_printf(
        "name=%s; content=%s; wrote=0; "
        "for d in \"${TMPDIR:-/tmp}\" \"$HOME\"; do "
        "[ -n \"$d\" ] || continue; "
        "[ -d \"$d\" ] || continue; "
        "[ -w \"$d\" ] || continue; "
        "rm -f \"$d/$name\" 2>/dev/null || true; "
        "if printf '%%s' \"$content\" > \"$d/$name\"; then wrote=1; break; fi; "
        "done; "
        "if [ \"$wrote\" = 1 ]; then "
        "echo 'RSFCLIENT_READY port=%d'; "
        "else "
        "echo 'rsfclient: warning: could not write pair config' >&2; "
        "fi; "
        "while :; do sleep 3600; done",
        quoted_name, quoted_content, rt->profile->remote_port);

    g_free(quoted_content);
    g_free(quoted_name);
    g_free(content);
    g_free(remote_name);
    return cmd;
}

static gchar **build_pair_upload_ssh_argv(Runtime *rt)
{
    Profile *p = rt->profile;
    GPtrArray *arr = g_ptr_array_new_with_free_func(g_free);
    gchar *target;
    gchar *port_s;
    gchar *identity_path = NULL;
    gchar **extra = NULL;
    gchar *remote_name;
    gchar *quoted_name;
    gchar *remote_cmd;
    int i;

    g_ptr_array_add(arr, g_strdup(str_empty(p->ssh_cmd) ? "ssh" : p->ssh_cmd));
    g_ptr_array_add(arr, g_strdup("-p"));

    port_s = g_strdup_printf("%d", p->ssh_port);
    g_ptr_array_add(arr, port_s);

    g_ptr_array_add(arr, g_strdup("-o"));
    g_ptr_array_add(arr, g_strdup("StrictHostKeyChecking=accept-new"));
    g_ptr_array_add(arr, g_strdup("-o"));
    g_ptr_array_add(arr, g_strdup("NumberOfPasswordPrompts=1"));

    if (g_strcmp0(p->auth_method, "password") == 0) {
        g_ptr_array_add(arr, g_strdup("-o"));
        g_ptr_array_add(arr, g_strdup("PreferredAuthentications=password,keyboard-interactive"));
        g_ptr_array_add(arr, g_strdup("-o"));
        g_ptr_array_add(arr, g_strdup("PubkeyAuthentication=no"));
    } else if (g_strcmp0(p->auth_method, "identity") == 0 && !str_empty(p->identity_file)) {
        identity_path = expand_home_path(p->identity_file);
        g_ptr_array_add(arr, g_strdup("-i"));
        g_ptr_array_add(arr, identity_path);
        identity_path = NULL;
        g_ptr_array_add(arr, g_strdup("-o"));
        g_ptr_array_add(arr, g_strdup("IdentitiesOnly=yes"));
    }

    if (!str_empty(p->extra_ssh_args)) {
        GError *err = NULL;
        if (g_shell_parse_argv(p->extra_ssh_args, NULL, &extra, &err)) {
            for (i = 0; extra[i]; i++) {
                g_ptr_array_add(arr, g_strdup(extra[i]));
            }
            g_strfreev(extra);
        } else if (err) {
            g_error_free(err);
        }
    }

    if (!str_empty(p->user)) {
        target = g_strdup_printf("%s@%s", p->user, p->host ? p->host : "");
    } else {
        target = g_strdup(p->host ? p->host : "");
    }
    g_ptr_array_add(arr, target);

    remote_name = pair_config_remote_name(p);
    quoted_name = shell_single_quote(remote_name);
    remote_cmd = g_strdup_printf(
        "name=%s; "
        "for d in \"${TMPDIR:-/tmp}\" \"$HOME\"; do "
        "[ -n \"$d\" ] || continue; "
        "[ -d \"$d\" ] || continue; "
        "[ -w \"$d\" ] || continue; "
        "umask 077; cat > \"$d/$name\" && exit 0; "
        "done; exit 1",
        quoted_name);
    g_ptr_array_add(arr, remote_cmd);
    g_ptr_array_add(arr, NULL);

    g_free(quoted_name);
    g_free(remote_name);
    g_free(identity_path);
    return (gchar **)g_ptr_array_free(arr, FALSE);
}

static gchar *ssh_argv_log_string(gchar **argv)
{
    GPtrArray *masked;
    gchar *cmdline;
    int i;

    if (!argv) return g_strdup("");

    masked = g_ptr_array_new_with_free_func(g_free);

    for (i = 0; argv[i]; i++) {
        if (g_strstr_len(argv[i], -1, "RSFCLIENT_READY") ||
            g_strstr_len(argv[i], -1, "token=")) {
            g_ptr_array_add(masked, g_strdup("[remote rsfclient pair setup]"));
        } else {
            g_ptr_array_add(masked, g_strdup(argv[i]));
        }
    }

    g_ptr_array_add(masked, NULL);
    cmdline = g_strjoinv(" ", (gchar **)masked->pdata);
    g_ptr_array_free(masked, TRUE);

    return cmdline;
}

static void launcher_set_askpass_environment(GSubprocessLauncher *launcher,
                                             Profile *p,
                                             const gchar *askpass_cancel_path);

static gboolean upload_pair_config(Runtime *rt)
{
    gchar **argv = NULL;
    gchar *content = NULL;
    gchar *stderr_text = NULL;
    GSubprocessLauncher *launcher = NULL;
    GSubprocess *proc = NULL;
    GError *err = NULL;
    gboolean ok = FALSE;

    if (!rt || rt->pair_uploaded) return TRUE;

    argv = build_pair_upload_ssh_argv(rt);
    content = pair_config_content(rt);

    launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDIN_PIPE |
        G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
        G_SUBPROCESS_FLAGS_STDERR_PIPE
    );
    launcher_set_askpass_environment(launcher, rt->profile, rt->askpass_cancel_path);
    proc = g_subprocess_launcher_spawnv(launcher, (const gchar * const *)argv, &err);
    g_object_unref(launcher);

    if (!proc) {
        app_log(rt->app, err ? err->message : "failed to start pair config upload", "warning");
        g_clear_error(&err);
        goto done;
    }

    if (!g_subprocess_communicate_utf8(proc, content, NULL, NULL, &stderr_text, &err)) {
        app_log(rt->app, err ? err->message : "failed to upload pair config", "warning");
        g_clear_error(&err);
        goto done;
    }

    if (g_subprocess_get_if_exited(proc) &&
        g_subprocess_get_exit_status(proc) == 0) {
        gchar *msg = g_strdup_printf("uploaded server pair config for port %d",
                                     rt->profile->remote_port);
        app_log(rt->app, msg, "success");
        g_free(msg);
        rt->pair_uploaded = TRUE;
        ok = TRUE;
    } else {
        gchar *msg = g_strdup_printf("server pair config upload failed%s%s",
                                     str_empty(stderr_text) ? "" : ": ",
                                     str_empty(stderr_text) ? "" : stderr_text);
        app_log(rt->app, msg, "warning");
        g_free(msg);
    }

done:
    if (proc) g_object_unref(proc);
    g_strfreev(argv);
    g_free(content);
    g_free(stderr_text);
    return ok;
}

static void launcher_set_askpass_environment(GSubprocessLauncher *launcher,
                                             Profile *p,
                                             const gchar *askpass_cancel_path)
{
    const gchar *display;
    const gchar *wayland;

    if (!str_empty(self_exe_path)) {
        g_subprocess_launcher_setenv(launcher, "SSH_ASKPASS", self_exe_path, TRUE);
    }

    g_subprocess_launcher_setenv(launcher, "SSH_ASKPASS_REQUIRE", "force", TRUE);
    g_subprocess_launcher_setenv(launcher, "RSFCLIENT_ASKPASS", "1", TRUE);
    if (!str_empty(askpass_cancel_path)) {
        g_subprocess_launcher_setenv(launcher, "RSFCLIENT_ASKPASS_CANCEL_FILE",
                                     askpass_cancel_path, TRUE);
    }

    display = g_getenv("DISPLAY");
    wayland = g_getenv("WAYLAND_DISPLAY");
    if (str_empty(display) && str_empty(wayland)) {
        g_subprocess_launcher_setenv(launcher, "DISPLAY", "rsfclient-askpass", TRUE);
    }

    if (p && g_strcmp0(p->auth_method, "password") == 0 && !str_empty(p->password)) {
        g_subprocess_launcher_setenv(launcher, "RSFCLIENT_ASKPASS_PASSWORD", p->password, TRUE);
    } else {
        g_subprocess_launcher_unsetenv(launcher, "RSFCLIENT_ASKPASS_PASSWORD");
    }
}

#ifdef G_OS_WIN32
static gchar **make_askpass_environment(Profile *p, const gchar *askpass_cancel_path)
{
    gchar **envp = g_get_environ();
    const gchar *display;
    const gchar *wayland;

    if (!str_empty(self_exe_path)) {
        envp = g_environ_setenv(envp, "SSH_ASKPASS", self_exe_path, TRUE);
    }

    envp = g_environ_setenv(envp, "SSH_ASKPASS_REQUIRE", "force", TRUE);
    envp = g_environ_setenv(envp, "RSFCLIENT_ASKPASS", "1", TRUE);
    if (!str_empty(askpass_cancel_path)) {
        envp = g_environ_setenv(envp, "RSFCLIENT_ASKPASS_CANCEL_FILE",
                                askpass_cancel_path, TRUE);
    }

    display = g_environ_getenv(envp, "DISPLAY");
    wayland = g_environ_getenv(envp, "WAYLAND_DISPLAY");
    if (str_empty(display) && str_empty(wayland)) {
        envp = g_environ_setenv(envp, "DISPLAY", "rsfclient-askpass", TRUE);
    }

    if (p && g_strcmp0(p->auth_method, "password") == 0 && !str_empty(p->password)) {
        envp = g_environ_setenv(envp, "RSFCLIENT_ASKPASS_PASSWORD", p->password, TRUE);
    } else {
        envp = g_environ_unsetenv(envp, "RSFCLIENT_ASKPASS_PASSWORD");
    }

    return envp;
}

static gchar *win32_error_message(DWORD code)
{
    LPWSTR wmsg = NULL;
    gchar *msg = NULL;

    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                   FORMAT_MESSAGE_FROM_SYSTEM |
                   FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, code, 0, (LPWSTR)&wmsg, 0, NULL);

    if (wmsg) {
        msg = g_utf16_to_utf8((const gunichar2 *)wmsg, -1, NULL, NULL, NULL);
        LocalFree(wmsg);
    }

    if (!msg) {
        msg = g_strdup_printf("Windows error %lu", (unsigned long)code);
    }

    g_strchomp(msg);
    return msg;
}

static void win_append_quoted_arg(GString *s, const gchar *arg)
{
    const gchar *p;
    int backslashes = 0;

    g_string_append_c(s, '"');

    for (p = arg ? arg : ""; *p; p++) {
        if (*p == '\\') {
            backslashes++;
        } else if (*p == '"') {
            while (backslashes-- > 0) g_string_append(s, "\\\\");
            g_string_append(s, "\\\"");
            backslashes = 0;
        } else {
            while (backslashes-- > 0) g_string_append_c(s, '\\');
            g_string_append_c(s, *p);
            backslashes = 0;
        }
    }

    while (backslashes-- > 0) g_string_append(s, "\\\\");
    g_string_append_c(s, '"');
}

static gchar *win_resolve_program_path(const gchar *program)
{
    gchar *found;

    if (str_empty(program)) return g_strdup("ssh");

    if (g_path_is_absolute(program) ||
        strchr(program, G_DIR_SEPARATOR) ||
        strchr(program, '/') ||
        strchr(program, '\\')) {
        return g_strdup(program);
    }

    found = g_find_program_in_path(program);
    return found ? found : g_strdup(program);
}

static gboolean win_program_is_unqualified(const gchar *program)
{
    return !str_empty(program) &&
           !g_path_is_absolute(program) &&
           !strchr(program, G_DIR_SEPARATOR) &&
           !strchr(program, '/') &&
           !strchr(program, '\\');
}

static gchar *win_build_command_line(const gchar *resolved_program, gchar **argv)
{
    GString *s = g_string_new(NULL);
    int i;

    win_append_quoted_arg(s, resolved_program);

    for (i = 1; argv && argv[i]; i++) {
        g_string_append_c(s, ' ');
        win_append_quoted_arg(s, argv[i]);
    }

    return g_string_free(s, FALSE);
}

static gunichar2 *win_env_block_from_strv(gchar **envp)
{
    GArray *arr = g_array_new(FALSE, FALSE, sizeof(gunichar2));
    gunichar2 zero = 0;
    int i;

    for (i = 0; envp && envp[i]; i++) {
        glong items_written = 0;
        gunichar2 *w = g_utf8_to_utf16(envp[i], -1, NULL, &items_written, NULL);

        if (!w) continue;

        g_array_append_vals(arr, w, (guint)items_written + 1);
        g_free(w);
    }

    g_array_append_val(arr, zero);
    return (gunichar2 *)g_array_free(arr, FALSE);
}

static gboolean win_spawn_ssh_process(Runtime *rt, gchar **argv, GError **err)
{
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    HANDLE stdout_read = NULL;
    HANDLE stdout_write = NULL;
    HANDLE nul_stdin = NULL;
    gchar *resolved_program = NULL;
    gchar *cmdline_utf8 = NULL;
    gunichar2 *app_w = NULL;
    gunichar2 *cmdline_w = NULL;
    gunichar2 *env_block = NULL;
    gchar **envp = NULL;
    gboolean ok = FALSE;
    gboolean app_name_is_qualified;
    DWORD flags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT;

    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0)) {
        gchar *msg = win32_error_message(GetLastError());
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "failed to create ssh output pipe: %s", msg);
        g_free(msg);
        goto done;
    }
    SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);

    nul_stdin = CreateFileW(L"NUL", GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (nul_stdin == INVALID_HANDLE_VALUE) nul_stdin = NULL;

    resolved_program = win_resolve_program_path(argv && argv[0] ? argv[0] : "ssh");
    app_name_is_qualified = !win_program_is_unqualified(resolved_program);
    cmdline_utf8 = win_build_command_line(resolved_program, argv);
    if (app_name_is_qualified) {
        app_w = g_utf8_to_utf16(resolved_program, -1, NULL, NULL, NULL);
    }
    cmdline_w = g_utf8_to_utf16(cmdline_utf8, -1, NULL, NULL, NULL);
    envp = make_askpass_environment(rt->profile, rt->askpass_cancel_path);
    env_block = win_env_block_from_strv(envp);

    if ((app_name_is_qualified && !app_w) || !cmdline_w || !env_block) {
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "failed to prepare Windows ssh process arguments");
        goto done;
    }

    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul_stdin ? nul_stdin : GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = stdout_write;
    si.hStdError = stdout_write;

    ok = CreateProcessW(app_name_is_qualified ? (LPCWSTR)app_w : NULL,
                        (LPWSTR)cmdline_w,
                        NULL, NULL, TRUE, flags, env_block,
                        NULL, &si, &pi);
    if (!ok) {
        gchar *msg = win32_error_message(GetLastError());
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "failed to start ssh without console window: %s", msg);
        g_free(msg);
        goto done;
    }

    rt->win_ssh_proc = g_new0(WinSshProcess, 1);
    rt->win_ssh_proc->pi = pi;
    rt->win_ssh_proc->stdout_fd =
        _open_osfhandle((intptr_t)stdout_read, _O_RDONLY | _O_BINARY);
    if (rt->win_ssh_proc->stdout_fd < 0) {
        CloseHandle(stdout_read);
    }
    stdout_read = NULL;

done:
    if (stdout_write) CloseHandle(stdout_write);
    if (stdout_read) CloseHandle(stdout_read);
    if (nul_stdin) CloseHandle(nul_stdin);
    g_free(resolved_program);
    g_free(cmdline_utf8);
    g_free(app_w);
    g_free(cmdline_w);
    g_free(env_block);
    if (envp) g_strfreev(envp);
    return ok;
}
#endif

static gboolean spawn_ssh_process(Runtime *rt, gchar **argv, GError **err)
{
#ifdef G_OS_WIN32
    return win_spawn_ssh_process(rt, argv, err);
#else
    GSubprocessLauncher *launcher;
    GSubprocess *proc;

    launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDIN_PIPE |
        G_SUBPROCESS_FLAGS_STDOUT_PIPE |
        G_SUBPROCESS_FLAGS_STDERR_MERGE
    );

    launcher_set_askpass_environment(launcher, rt->profile, rt->askpass_cancel_path);

    proc = g_subprocess_launcher_spawnv(
        launcher,
        (const gchar * const *)argv,
        err
    );

    g_object_unref(launcher);
    rt->ssh_proc = proc;
    return proc != NULL;
#endif
}

typedef struct {
#ifdef G_OS_WIN32
    HANDLE process_handle;
#else
    GSubprocess *proc;
#endif
    GMutex mutex;
    GCond cond;
    gboolean exited;
    gint exit_code;
} WaitState;

static gpointer subprocess_wait_thread(gpointer data)
{
    WaitState *ws = (WaitState *)data;
#ifdef G_OS_WIN32
    DWORD exit_code = 0;

    WaitForSingleObject(ws->process_handle, INFINITE);

    g_mutex_lock(&ws->mutex);
    ws->exited = TRUE;

    if (GetExitCodeProcess(ws->process_handle, &exit_code)) {
        ws->exit_code = (gint)exit_code;
    } else {
        ws->exit_code = -1;
    }

    g_cond_broadcast(&ws->cond);
    g_mutex_unlock(&ws->mutex);
#else
    GError *err = NULL;

    g_subprocess_wait(ws->proc, NULL, &err);

    g_mutex_lock(&ws->mutex);
    ws->exited = TRUE;

    if (g_subprocess_get_if_exited(ws->proc)) {
        ws->exit_code = g_subprocess_get_exit_status(ws->proc);
    } else {
        ws->exit_code = -1;
    }

    g_cond_broadcast(&ws->cond);
    g_mutex_unlock(&ws->mutex);

    if (err) g_error_free(err);
#endif

    return NULL;
}

static void handle_ssh_output_line(Runtime *rt, const gchar *line)
{
    gchar *msg;

    if (!rt || str_empty(line)) return;

    if (g_str_has_prefix(line, "RSFCLIENT_READY")) {
        g_mutex_lock(&rt->lock);
        rt->tunnel_ready = TRUE;
        rt->pair_uploaded = TRUE;
        g_mutex_unlock(&rt->lock);

        msg = g_strdup_printf("ssh tunnel established; %s", line);
        app_log(rt->app, msg, "success");
        g_free(msg);
        return;
    }

    msg = g_strdup_printf("ssh: %s", line);
    if (is_auth_failure_text(line)) {
        runtime_mark_auth_failure(rt, line);
    }
    app_log(rt->app, msg, is_error_text(line) ? "error" : "info");
    g_free(msg);
}

static gpointer ssh_output_thread(gpointer data)
{
#ifdef G_OS_WIN32
    WinSshOutputData *d = (WinSshOutputData *)data;
    Runtime *rt;
    FILE *fp;
    char buf[2048];

    if (!d) return NULL;
    rt = d->rt;

    fp = fdopen(d->fd, "r");
    if (!fp) {
        g_close(d->fd, NULL);
        g_free(d);
        return NULL;
    }

    while (fgets(buf, sizeof(buf), fp) != NULL) {
        gchar *line = g_strdup(buf);
        g_strchomp(line);
        handle_ssh_output_line(rt, line);
        g_free(line);
    }

    fclose(fp);
    g_free(d);
#else
    Runtime *rt = (Runtime *)data;
    GInputStream *in;
    GDataInputStream *din;
    gchar *line;
    gsize len;
    GError *err = NULL;

    if (!rt->ssh_proc) return NULL;

    in = g_subprocess_get_stdout_pipe(rt->ssh_proc);
    if (!in) return NULL;

    din = g_data_input_stream_new(in);

    while ((line = g_data_input_stream_read_line(din, &len, NULL, &err)) != NULL) {
        handle_ssh_output_line(rt, line);
        g_free(line);
    }

    if (err) g_error_free(err);
    g_object_unref(din);
#endif

    return NULL;
}

static void start_ssh_output_thread(Runtime *rt)
{
#ifdef G_OS_WIN32
    WinSshOutputData *d;

    if (!rt || !rt->win_ssh_proc || rt->win_ssh_proc->stdout_fd < 0) return;

    d = g_new0(WinSshOutputData, 1);
    d->rt = rt;
    d->fd = rt->win_ssh_proc->stdout_fd;
    rt->win_ssh_proc->stdout_fd = -1;
    g_thread_unref(g_thread_new("ssh-output", ssh_output_thread, d));
#else
    if (!rt || !rt->ssh_proc) return;
    g_thread_unref(g_thread_new("ssh-output", ssh_output_thread, rt));
#endif
}

static void runtime_force_exit_ssh(Runtime *rt)
{
    if (!rt) return;

#ifdef G_OS_WIN32
    if (rt->win_ssh_proc && rt->win_ssh_proc->pi.hProcess) {
        TerminateProcess(rt->win_ssh_proc->pi.hProcess, 1);
    }
#else
    if (rt->ssh_proc) {
        g_subprocess_force_exit(rt->ssh_proc);
    }
#endif
}

static void runtime_clear_ssh_process(Runtime *rt)
{
    if (!rt) return;

#ifdef G_OS_WIN32
    if (rt->win_ssh_proc) {
        if (rt->win_ssh_proc->stdout_fd >= 0) {
            _close(rt->win_ssh_proc->stdout_fd);
        }
        if (rt->win_ssh_proc->pi.hThread) {
            CloseHandle(rt->win_ssh_proc->pi.hThread);
        }
        if (rt->win_ssh_proc->pi.hProcess) {
            CloseHandle(rt->win_ssh_proc->pi.hProcess);
        }
        g_free(rt->win_ssh_proc);
        rt->win_ssh_proc = NULL;
    }
#else
    if (rt->ssh_proc) {
        g_object_unref(rt->ssh_proc);
        rt->ssh_proc = NULL;
    }
#endif
}

static gpointer tunnel_thread_func(gpointer data)
{
    Runtime *rt = (Runtime *)data;
    int retries = 0;

    while (!runtime_is_stopping(rt)) {
        gchar **argv = build_ssh_argv(rt);
        gchar *cmdline = ssh_argv_log_string(argv);
        GError *err = NULL;
        WaitState ws;
        GThread *wait_thread;
        gboolean early_exit;
        gint64 until;

        runtime_clear_auth_failure(rt);
        g_mutex_lock(&rt->lock);
        rt->tunnel_ready = FALSE;
        rt->pair_uploaded = FALSE;
        g_mutex_unlock(&rt->lock);

        {
            gchar *msg = g_strdup_printf("starting ssh tunnel: %s", cmdline);
            app_log(rt->app, msg, "info");
            g_free(msg);
        }

        if (!spawn_ssh_process(rt, argv, &err)) {
            retries++;

            g_free(cmdline);
            g_strfreev(argv);

            app_log(rt->app, err ? err->message : "failed to start ssh", "error");
            if (err) g_error_free(err);

            if (retries > MAX_RETRIES) {
                app_log(rt->app, "ssh tunnel retry limit reached (3); stop reconnecting", "error");
                break;
            }

            {
                gchar *msg = g_strdup_printf("retry ssh tunnel %d/%d in %d seconds",
                                             retries, MAX_RETRIES, RETRY_DELAY_SEC);
                app_log(rt->app, msg, "info");
                g_free(msg);
            }

            g_usleep(RETRY_DELAY_SEC * G_USEC_PER_SEC);
            continue;
        }

        g_free(cmdline);
        g_strfreev(argv);

        start_ssh_output_thread(rt);

        g_mutex_init(&ws.mutex);
        g_cond_init(&ws.cond);
#ifdef G_OS_WIN32
        ws.process_handle = rt->win_ssh_proc->pi.hProcess;
#else
        ws.proc = rt->ssh_proc;
#endif
        ws.exited = FALSE;
        ws.exit_code = -1;

        wait_thread = g_thread_new("ssh-wait", subprocess_wait_thread, &ws);

        g_mutex_lock(&ws.mutex);
        until = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
        while (!ws.exited) {
            if (!g_cond_wait_until(&ws.cond, &ws.mutex, until)) break;
        }
        early_exit = ws.exited;
        g_mutex_unlock(&ws.mutex);

        if (!early_exit) {
            retries = 0;
            app_log(rt->app, "ssh process started; waiting for tunnel confirmation", "info");
        }

        g_mutex_lock(&ws.mutex);
        while (!ws.exited) {
            g_cond_wait(&ws.cond, &ws.mutex);
        }
        g_mutex_unlock(&ws.mutex);

        g_thread_join(wait_thread);

        if (runtime_is_stopping(rt)) {
            runtime_clear_ssh_process(rt);
            g_mutex_clear(&ws.mutex);
            g_cond_clear(&ws.cond);
            break;
        }

        {
            gchar *reason = NULL;
            gchar *msg = NULL;

            if (runtime_get_auth_failure(rt, &reason)) {
                msg = g_strdup_printf("ssh authentication failed: %s", reason ? reason : "unknown reason");
            } else {
                msg = g_strdup_printf("ssh tunnel disconnected with code %d", ws.exit_code);
            }
            app_log(rt->app, msg, "error");
            g_free(msg);
            g_free(reason);
        }

        runtime_clear_ssh_process(rt);

        if (runtime_get_auth_failure(rt, NULL)) {
            app_log(rt->app, "authentication failed; not retrying automatically", "error");
            g_mutex_clear(&ws.mutex);
            g_cond_clear(&ws.cond);
            break;
        }

        retries++;

        if (retries > MAX_RETRIES) {
            app_log(rt->app, "ssh tunnel disconnected and retry limit reached (3); stop reconnecting", "error");
            g_mutex_clear(&ws.mutex);
            g_cond_clear(&ws.cond);
            break;
        }

        {
            gchar *msg = g_strdup_printf("retry ssh tunnel %d/%d in %d seconds",
                                         retries, MAX_RETRIES, RETRY_DELAY_SEC);
            app_log(rt->app, msg, "info");
            g_free(msg);
        }

        g_mutex_clear(&ws.mutex);
        g_cond_clear(&ws.cond);

        g_usleep(RETRY_DELAY_SEC * G_USEC_PER_SEC);
    }

    return NULL;
}

static gpointer listener_thread_func(gpointer data)
{
    Runtime *rt = (Runtime *)data;

    while (!runtime_is_stopping(rt)) {
        GError *err = NULL;
        GSocketConnection *conn;

        conn = g_socket_listener_accept(rt->listener, NULL, rt->cancel, &err);

        if (!conn) {
            if (!runtime_is_stopping(rt)) {
                if (err && g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                    /* Normal disconnect path. */
                } else {
                    app_log(rt->app, err ? err->message : "accept failed", "error");
                }
            }
            if (err) g_error_free(err);
            break;
        }

        {
            GPtrArray *arr = g_ptr_array_new();
            g_ptr_array_add(arr, rt);
            g_ptr_array_add(arr, conn);
            g_thread_new("rsfclient-connection", connection_thread, arr);
        }
    }

    app_log(rt->app, "listener stopped", "info");

    return NULL;
}

static Runtime *runtime_new(AppState *app, Profile *profile)
{
    Runtime *rt = g_new0(Runtime, 1);
    gint session_port;
    gchar *salt;
    gchar *hash;

    rt->app = app;
    rt->profile = profile_copy(profile);
    session_port = g_random_int_range(RANDOM_PORT_MIN, RANDOM_PORT_MAX + 1);
    if (rt->profile->local_port <= 0 && rt->profile->remote_port <= 0) {
        rt->profile->local_port = session_port;
        rt->profile->remote_port = session_port;
    } else if (rt->profile->local_port <= 0) {
        rt->profile->local_port = rt->profile->remote_port;
    } else if (rt->profile->remote_port <= 0) {
        rt->profile->remote_port = rt->profile->local_port;
    }

    rt->pair_token = g_uuid_string_random();
    salt = make_salt();
    hash = sha256_hex_with_salt(salt, rt->pair_token);
    g_free(rt->profile->token_salt);
    g_free(rt->profile->token_hash);
    rt->profile->token_salt = salt;
    rt->profile->token_hash = hash;

    rt->listener = NULL;
    rt->cancel = g_cancellable_new();
    rt->ssh_proc = NULL;
    rt->stopping = FALSE;
    rt->askpass_cancel_path = make_askpass_cancel_path();
    if (!str_empty(rt->askpass_cancel_path)) {
        g_remove(rt->askpass_cancel_path);
    }
    g_mutex_init(&rt->lock);

    return rt;
}

static gboolean runtime_start(Runtime *rt)
{
    GInetAddress *addr;
    GSocketAddress *saddr;
    GError *err = NULL;
    gboolean ok;

    rt->listener = g_socket_listener_new();

    addr = g_inet_address_new_from_string(str_empty(rt->profile->local_host) ?
                                          "127.0.0.1" : rt->profile->local_host);
    if (!addr) {
        app_log(rt->app, "invalid local listen address", "error");
        return FALSE;
    }

    saddr = g_inet_socket_address_new(addr, rt->profile->local_port);
    g_object_unref(addr);

    ok = g_socket_listener_add_address(rt->listener, saddr,
                                       G_SOCKET_TYPE_STREAM,
                                       G_SOCKET_PROTOCOL_TCP,
                                       NULL, NULL, &err);
    g_object_unref(saddr);

    if (!ok) {
        app_log(rt->app, err ? err->message : "failed to listen", "error");
        if (err) g_error_free(err);
        return FALSE;
    }

    {
        gchar *msg = g_strdup_printf("listening on %s:%d",
                                     rt->profile->local_host, rt->profile->local_port);
        app_log(rt->app, msg, "success");
        g_free(msg);
    }

    rt->listener_thread = g_thread_new("rsfclient-listener", listener_thread_func, rt);
    rt->tunnel_thread = g_thread_new("rsfclient-tunnel", tunnel_thread_func, rt);

    return TRUE;
}

static void runtime_stop(Runtime *rt)
{
    if (!rt) return;

    app_log(rt->app, "disconnect requested", "info");
    runtime_set_stopping(rt, TRUE);

    if (!str_empty(rt->askpass_cancel_path)) {
        g_file_set_contents(rt->askpass_cancel_path, "cancel\n", -1, NULL);
    }

    /*
       Interrupt the blocking listener accept() immediately.  Without this,
       Windows may keep 127.0.0.1:17890 occupied until the accept thread returns.
    */
    if (rt->cancel) {
        g_cancellable_cancel(rt->cancel);
    }

    if (rt->listener) {
        g_socket_listener_close(rt->listener);
    }

    runtime_force_exit_ssh(rt);
}

static void runtime_free(Runtime *rt)
{
    if (!rt) return;

    runtime_stop(rt);

    if (rt->listener_thread) {
        g_thread_join(rt->listener_thread);
    }

    if (rt->tunnel_thread) {
        g_thread_join(rt->tunnel_thread);
    }

    if (rt->listener) g_object_unref(rt->listener);
    if (rt->cancel) g_object_unref(rt->cancel);
    runtime_clear_ssh_process(rt);

    profile_free(rt->profile);
    g_free(rt->auth_failure_reason);
    if (!str_empty(rt->askpass_cancel_path)) {
        g_remove(rt->askpass_cancel_path);
    }
    g_free(rt->askpass_cancel_path);
    g_free(rt->pair_token);
    g_mutex_clear(&rt->lock);
    g_free(rt);
}

static gpointer runtime_free_thread_func(gpointer data)
{
    runtime_free((Runtime *)data);
    return NULL;
}

static void runtime_free_async(Runtime *rt)
{
    if (!rt) return;

    /*
       Do not join listener / ssh threads in the GTK main thread.  Disconnect
       should return immediately; the cleanup can finish in the background.
    */
    runtime_stop(rt);
    g_thread_unref(g_thread_new("rsfclient-runtime-free", runtime_free_thread_func, rt));
}

/* ---------------------------------------------------------------------- */
/* UI helpers                                                              */
/* ---------------------------------------------------------------------- */

static void install_app_css(void)
{
    GtkCssProvider *provider = gtk_css_provider_new();
    GdkDisplay *display = gdk_display_get_default();
    const gchar *css =
        ".key-label { color: #0b5cad; }"
        ".key-entry { border-color: #0b5cad; }"
        ".green-action {"
        "  background: #2e7d32;"
        "  background-image: none;"
        "  color: white;"
        "  border-color: #256d2c;"
        "}"
        ".green-action:disabled {"
        "  background: #8dbb91;"
        "  background-image: none;"
        "  color: rgba(255,255,255,0.75);"
        "}";

#if GTK_CHECK_VERSION(4, 12, 0)
    gtk_css_provider_load_from_string(provider, css);
#else
    gtk_css_provider_load_from_data(provider, css, -1);
#endif

    if (display) {
        gtk_style_context_add_provider_for_display(
            display,
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
        );
    }

    g_object_unref(provider);
}

static void clear_main(AppState *app)
{
    GtkWidget *child;

    /*
       The log view belongs to the current screen.  After removing the screen,
       background runtime threads must not keep appending to the old TextBuffer.
    */
    app->log_view = NULL;
    app->log_buffer = NULL;
    app->main_connect_button = NULL;
    app->main_edit_button = NULL;
    app->main_delete_button = NULL;

    while ((child = gtk_widget_get_first_child(app->main_box)) != NULL) {
        gtk_box_remove(GTK_BOX(app->main_box), child);
    }
}

static GtkWidget *make_button(const gchar *label, GCallback cb, gpointer data)
{
    GtkWidget *b = gtk_button_new_with_label(label);
    g_signal_connect(b, "clicked", cb, data);
    return b;
}

static GtkWidget *make_green_button(const gchar *label, GCallback cb, gpointer data)
{
    GtkWidget *b = make_button(label, cb, data);
    gtk_widget_add_css_class(b, "green-action");
    return b;
}

static GtkWidget *entry_new_text(const gchar *s)
{
    GtkWidget *e = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(e), s ? s : "");
    return e;
}

static const gchar *entry_text(GtkWidget *e)
{
    return gtk_editable_get_text(GTK_EDITABLE(e));
}

static GtkWidget *form_label_new(const gchar *label, gboolean important)
{
    GtkWidget *l = gtk_label_new(NULL);

    if (important) {
        gchar *markup = g_markup_printf_escaped("<b>%s</b>", label ? label : "");
        gtk_label_set_markup(GTK_LABEL(l), markup);
        gtk_widget_add_css_class(l, "key-label");
        g_free(markup);
    } else {
        gtk_label_set_text(GTK_LABEL(l), label ? label : "");
    }

    gtk_widget_set_halign(l, GTK_ALIGN_START);
    return l;
}

static void form_add_entry(AppState *app, GtkWidget *grid, int row,
                           const gchar *key, const gchar *label,
                           const gchar *value)
{
    GtkWidget *l = form_label_new(label, FALSE);
    GtkWidget *e = entry_new_text(value);

    gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), e, 1, row, 1, 1);

    g_hash_table_insert(app->form_entries, g_strdup(key), e);
}

typedef struct {
    GtkWidget *entry;
    gboolean folder;
} BrowseButtonData;

typedef struct {
    GtkWidget *entry;
} BrowseResponseData;

static void on_browse_response(GtkNativeDialog *dialog, gint response, gpointer data)
{
    BrowseResponseData *d = (BrowseResponseData *)data;

    if (response == GTK_RESPONSE_ACCEPT && d && d->entry) {
        GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        if (file) {
            gchar *path = g_file_get_path(file);
            if (!str_empty(path)) {
                gtk_editable_set_text(GTK_EDITABLE(d->entry), path);
            }
            g_free(path);
            g_object_unref(file);
        }
    }

    if (d && d->entry) {
        g_object_unref(d->entry);
    }
    g_free(d);
    g_object_unref(dialog);
}

static void on_browse_clicked(GtkButton *button, gpointer data)
{
    AppState *app = (AppState *)data;
    BrowseButtonData *bd = g_object_get_data(G_OBJECT(button), "browse-data");
    BrowseResponseData *rd;
    GtkFileChooserAction action;
    GtkFileChooserNative *dialog;
    const gchar *current;

    if (!bd || !bd->entry) return;

    action = bd->folder ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER : GTK_FILE_CHOOSER_ACTION_OPEN;
    dialog = gtk_file_chooser_native_new(
        bd->folder ? "Select folder" : "Select file",
        app && app->window ? GTK_WINDOW(app->window) : NULL,
        action,
        "Select",
        "Cancel"
    );

    current = entry_text(bd->entry);
    if (!str_empty(current)) {
        gchar *expanded = expand_home_path(current);
        gchar *folder_path = NULL;
        GFile *folder = NULL;

        if (bd->folder) {
            folder_path = g_strdup(expanded);
        } else {
            folder_path = g_path_get_dirname(expanded);
        }

        if (!str_empty(folder_path)) {
            folder = g_file_new_for_path(folder_path);
            gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), folder, NULL);
            g_object_unref(folder);
        }

        g_free(folder_path);
        g_free(expanded);
    }

    rd = g_new0(BrowseResponseData, 1);
    rd->entry = g_object_ref(bd->entry);
    g_signal_connect(dialog, "response", G_CALLBACK(on_browse_response), rd);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}

static void form_add_browse_entry(AppState *app, GtkWidget *grid, int row,
                                  const gchar *key, const gchar *label,
                                  const gchar *value, gboolean folder)
{
    GtkWidget *l = form_label_new(label, FALSE);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *e = entry_new_text(value);
    GtkWidget *browse = gtk_button_new_with_label("Browse");
    BrowseButtonData *bd = g_new0(BrowseButtonData, 1);

    bd->entry = e;
    bd->folder = folder;

    gtk_widget_set_hexpand(e, TRUE);
    g_object_set_data_full(G_OBJECT(browse), "browse-data", bd, g_free);
    g_signal_connect(browse, "clicked", G_CALLBACK(on_browse_clicked), app);

    gtk_box_append(GTK_BOX(box), e);
    gtk_box_append(GTK_BOX(box), browse);

    gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), box, 1, row, 1, 1);

    g_hash_table_insert(app->form_entries, g_strdup(key), e);
}

static void form_add_key_entry(AppState *app, GtkWidget *grid, int row,
                               const gchar *key, const gchar *label,
                               const gchar *value,
                               const gchar *placeholder)
{
    GtkWidget *l = form_label_new(label, TRUE);
    GtkWidget *e = entry_new_text(value);

    if (placeholder) {
        gtk_entry_set_placeholder_text(GTK_ENTRY(e), placeholder);
    }

    gtk_widget_add_css_class(e, "key-entry");
    gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), e, 1, row, 1, 1);

    g_hash_table_insert(app->form_entries, g_strdup(key), e);
}

static gchar *form_get(AppState *app, const gchar *key)
{
    GtkWidget *e = g_hash_table_lookup(app->form_entries, key);

    if (!e) return g_strdup("");

    return g_strdup(entry_text(e));
}

static gint form_get_int(AppState *app, const gchar *key, gint def)
{
    gchar *s = form_get(app, key);
    gint v = str_empty(s) ? def : atoi(s);
    g_free(s);
    return v;
}

typedef struct {
    const gchar *method;
    const gchar *label;
} AuthOption;

static const AuthOption AUTH_OPTIONS[] = {
    {"default", "Default keys / ssh-agent"},
    {"password", "Username / Password"},
    {"identity", "Identity file"},
    {NULL, NULL}
};

static guint auth_index_from_method(const gchar *method)
{
    guint i;

    for (i = 0; AUTH_OPTIONS[i].method; i++) {
        if (g_strcmp0(method, AUTH_OPTIONS[i].method) == 0) return i;
    }

    return 0;
}

static const gchar *auth_method_from_index(guint index)
{
    if (AUTH_OPTIONS[index].method) return AUTH_OPTIONS[index].method;
    return "default";
}

static void update_auth_visibility(AppState *app)
{
    guint selected;
    const gchar *method;

    if (!app || !app->form_auth_combo) return;

    selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->form_auth_combo));
    method = auth_method_from_index(selected);

    if (app->form_auth_default_box) {
        gtk_widget_set_visible(app->form_auth_default_box,
                               g_strcmp0(method, "default") == 0);
    }

    if (app->form_auth_password_box) {
        gtk_widget_set_visible(app->form_auth_password_box,
                               g_strcmp0(method, "password") == 0);
    }

    if (app->form_auth_identity_box) {
        gtk_widget_set_visible(app->form_auth_identity_box,
                               g_strcmp0(method, "identity") == 0);
    }
}

static void on_auth_selected_changed(GObject *object, GParamSpec *pspec, gpointer data)
{
    (void)object;
    (void)pspec;
    update_auth_visibility((AppState *)data);
}

static void on_advanced_toggled(GtkCheckButton *button, gpointer data)
{
    AppState *app = (AppState *)data;

    if (app && app->form_advanced_box) {
        gtk_widget_set_visible(app->form_advanced_box,
                               gtk_check_button_get_active(button));
    }
}

static void show_start_screen(AppState *app);

static void on_exit_clicked(GtkButton *b, gpointer data)
{
    AppState *app = (AppState *)data;

    /*
       On application exit we only request stop and let the process terminate.
       Joining worker threads here can freeze the window on slow SSH shutdown.
    */
    if (app->runtime) {
        runtime_stop(app->runtime);
        app->runtime = NULL;
    }

    g_application_quit(G_APPLICATION(app->gtk_app));
}

static void show_run_screen(AppState *app);

static void on_disconnect_clicked(GtkButton *b, gpointer data)
{
    AppState *app = (AppState *)data;
    Runtime *rt = app->runtime;

    if (rt) {
        app->runtime = NULL;
        runtime_free_async(rt);
    }

    show_start_screen(app);
}

static gboolean profile_validate_for_connect(Profile *p, gchar **message)
{
    if (!p) {
        if (message) *message = g_strdup("profile is empty");
        return FALSE;
    }

    if (str_empty(p->host)) {
        if (message) *message = g_strdup("Host is required.");
        return FALSE;
    }

    if (p->ssh_port <= 0 || p->ssh_port > 65535) {
        if (message) *message = g_strdup("Host port must be between 1 and 65535.");
        return FALSE;
    }

    if (g_strcmp0(p->auth_method, "password") == 0) {
        if (str_empty(p->user)) {
            if (message) *message = g_strdup("Username is required for Username / Password authentication.");
            return FALSE;
        }
    } else if (g_strcmp0(p->auth_method, "identity") == 0) {
        gchar *identity_path;
        gboolean exists;

        if (str_empty(p->identity_file)) {
            if (message) *message = g_strdup("Identity file authentication is selected, but no identity file is set.");
            return FALSE;
        }

        identity_path = expand_home_path(p->identity_file);
        exists = g_file_test(identity_path, G_FILE_TEST_IS_REGULAR);
        if (!exists) {
            if (message) *message = g_strdup_printf("Identity file does not exist: %s", identity_path);
            g_free(identity_path);
            return FALSE;
        }
        g_free(identity_path);
    }

    return TRUE;
}

static void connect_profile(AppState *app, Profile *p)
{
    gchar *summary;
    gchar *message = NULL;

    if (!p) return;

    if (!profile_validate_for_connect(p, &message)) {
        app_log(app, message ? message : "profile validation failed", "error");
        g_free(message);
        return;
    }

    show_run_screen(app);

    summary = profile_summary(p);
    {
        gchar *msg = g_strdup_printf("using profile: %s", summary);
        app_log(app, msg, "info");
        g_free(msg);
    }
    g_free(summary);

    app->runtime = runtime_new(app, p);

    if (!runtime_start(app->runtime)) {
        runtime_free(app->runtime);
        app->runtime = NULL;
    }
}

static guint get_selected_profile_index(AppState *app, gboolean *ok)
{
    GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(app->profile_list));
    int idx;

    if (!row) {
        *ok = FALSE;
        return 0;
    }

    idx = gtk_list_box_row_get_index(row);

    if (idx < 0 || !app->profiles || (guint)idx >= app->profiles->len) {
        *ok = FALSE;
        return 0;
    }

    *ok = TRUE;
    return (guint)idx;
}

static void update_profile_action_sensitivity(AppState *app)
{
    gboolean has_selection = FALSE;

    if (app && app->profile_list) {
        GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(app->profile_list));
        if (row) {
            gint idx = gtk_list_box_row_get_index(row);
            has_selection = idx >= 0 && app->profiles && (guint)idx < app->profiles->len;
        }
    }

    if (app->main_connect_button) {
        gtk_widget_set_sensitive(app->main_connect_button, has_selection);
    }
    if (app->main_edit_button) {
        gtk_widget_set_sensitive(app->main_edit_button, has_selection);
    }
    if (app->main_delete_button) {
        gtk_widget_set_sensitive(app->main_delete_button, has_selection);
    }
}

static void on_profile_selection_changed(GtkListBox *box, gpointer data)
{
    (void)box;
    update_profile_action_sensitivity((AppState *)data);
}

static void on_connect_clicked(GtkButton *b, gpointer data)
{
    AppState *app = (AppState *)data;
    gboolean ok;
    guint idx = get_selected_profile_index(app, &ok);

    if (!ok) return;

    connect_profile(app, app_get_profile_by_index(app, idx));
}

static void show_profile_form(AppState *app, Profile *existing);

static void on_new_profile_clicked(GtkButton *b, gpointer data)
{
    show_profile_form((AppState *)data, NULL);
}

static void on_edit_profile_clicked(GtkButton *b, gpointer data)
{
    AppState *app = (AppState *)data;
    gboolean ok;
    guint idx = get_selected_profile_index(app, &ok);

    if (!ok) return;

    show_profile_form(app, app_get_profile_by_index(app, idx));
}

typedef struct {
    AppState *app;
    gchar *group;
    gchar *name;
} DeleteProfileState;

static void on_delete_profile_confirm_response(GtkDialog *dialog,
                                               int response,
                                               gpointer user_data)
{
    DeleteProfileState *st = user_data;

    if (st && response == GTK_RESPONSE_ACCEPT) {
        g_key_file_remove_group(st->app->config, st->group, NULL);
        app_save_config(st->app);
        show_start_screen(st->app);
    }

    gtk_window_destroy(GTK_WINDOW(dialog));
    if (st) {
        g_free(st->group);
        g_free(st->name);
        g_free(st);
    }
}

static void on_delete_profile_clicked(GtkButton *b, gpointer data)
{
    AppState *app = (AppState *)data;
    gboolean ok;
    guint idx = get_selected_profile_index(app, &ok);
    Profile *p;
    DeleteProfileState *st;
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *label;
    gchar *msg;

    (void)b;
    if (!ok) return;

    p = app_get_profile_by_index(app, idx);
    if (!p) return;

    dialog = gtk_dialog_new_with_buttons("Delete profile",
                                         GTK_WINDOW(app->window),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "Delete",
                                         GTK_RESPONSE_ACCEPT,
                                         "Cancel",
                                         GTK_RESPONSE_CANCEL,
                                         NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    msg = g_strdup_printf("Delete profile \"%s\"?", str_empty(p->name) ? "(unnamed)" : p->name);
    label = gtk_label_new(msg);
    gtk_widget_set_margin_top(label, 12);
    gtk_widget_set_margin_bottom(label, 12);
    gtk_widget_set_margin_start(label, 12);
    gtk_widget_set_margin_end(label, 12);
    gtk_box_append(GTK_BOX(content), label);

    st = g_new0(DeleteProfileState, 1);
    st->app = app;
    st->group = profile_group_name(p->name);
    st->name = g_strdup(p->name);
    g_signal_connect(dialog, "response", G_CALLBACK(on_delete_profile_confirm_response), st);

    g_free(msg);
    gtk_window_present(GTK_WINDOW(dialog));
}

static void show_start_screen(AppState *app)
{
    guint i;

    clear_main(app);
    app_reload_profiles(app);

    {
        GtkWidget *title = gtk_label_new("RSF View Client");
        gtk_widget_add_css_class(title, "title-2");
        gtk_widget_set_halign(title, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(app->main_box), title);
    }

    {
        GtkWidget *desc = gtk_label_new("Select, edit, or create an SSH reverse-tunnel profile.");
        gtk_widget_set_halign(desc, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(app->main_box), desc);
    }

    app->profile_list = gtk_list_box_new();
    gtk_widget_set_vexpand(app->profile_list, TRUE);
    g_signal_connect(app->profile_list, "selected-rows-changed",
                     G_CALLBACK(on_profile_selection_changed), app);
    gtk_box_append(GTK_BOX(app->main_box), app->profile_list);

    for (i = 0; app->profiles && i < app->profiles->len; i++) {
        Profile *p = g_ptr_array_index(app->profiles, i);
        gchar *s = profile_summary(p);
        GtkWidget *label = gtk_label_new(s);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_list_box_append(GTK_LIST_BOX(app->profile_list), label);
        g_free(s);
    }

    if (app->profiles && app->profiles->len > 0) {
        GtkListBoxRow *row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(app->profile_list), 0);
        if (row) gtk_list_box_select_row(GTK_LIST_BOX(app->profile_list), row);
    }

    {
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

        app->main_connect_button = make_green_button("Connect", G_CALLBACK(on_connect_clicked), app);
        app->main_edit_button = make_button("Edit Profile", G_CALLBACK(on_edit_profile_clicked), app);
        app->main_delete_button = make_button("Delete Profile", G_CALLBACK(on_delete_profile_clicked), app);

        gtk_box_append(GTK_BOX(box), app->main_connect_button);
        gtk_box_append(GTK_BOX(box), make_button("New Profile", G_CALLBACK(on_new_profile_clicked), app));
        gtk_box_append(GTK_BOX(box), app->main_edit_button);
        gtk_box_append(GTK_BOX(box), app->main_delete_button);
        gtk_box_append(GTK_BOX(box), make_button("Exit", G_CALLBACK(on_exit_clicked), app));

        gtk_box_append(GTK_BOX(app->main_box), box);
    }

    update_profile_action_sensitivity(app);
}

static void show_run_screen(AppState *app)
{
    GtkWidget *btns;
    GtkWidget *sw;

    clear_main(app);

    btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(btns), make_button("Disconnect", G_CALLBACK(on_disconnect_clicked), app));
    gtk_box_append(GTK_BOX(btns), make_button("Exit", G_CALLBACK(on_exit_clicked), app));
    gtk_box_append(GTK_BOX(app->main_box), btns);

    app->log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(app->log_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(app->log_view), TRUE);
    app->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->log_view));

    gtk_text_buffer_create_tag(app->log_buffer, "error",
                               "foreground", "#cc0000",
                               "weight", PANGO_WEIGHT_BOLD,
                               NULL);
    gtk_text_buffer_create_tag(app->log_buffer, "success",
                               "foreground", "#008000",
                               "weight", PANGO_WEIGHT_BOLD,
                               NULL);
    gtk_text_buffer_create_tag(app->log_buffer, "warning",
                               "foreground", "#b77900",
                               "weight", PANGO_WEIGHT_BOLD,
                               NULL);
    gtk_text_buffer_create_tag(app->log_buffer, "info", NULL);

    sw = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(sw, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), app->log_view);
    gtk_box_append(GTK_BOX(app->main_box), sw);
}

static void save_form_profile(AppState *app, gboolean connect_after)
{
    Profile *p = profile_new_default();
    Profile *persisted = NULL;
    gchar *token;
    gchar *password;
    const gchar *auth_method;
    gboolean save_password = FALSE;

#define GET_STR_FIELD(field, key) do { g_free(p->field); p->field = form_get(app, key); } while (0)

    GET_STR_FIELD(name, "name");
    GET_STR_FIELD(ssh_cmd, "ssh_cmd");
    GET_STR_FIELD(host, "host");
    p->ssh_port = form_get_int(app, "ssh_port", 22);

    auth_method = auth_method_from_index(
        gtk_drop_down_get_selected(GTK_DROP_DOWN(app->form_auth_combo))
    );
    g_free(p->auth_method);
    p->auth_method = g_strdup(auth_method);

    if (g_strcmp0(p->auth_method, "identity") == 0) {
        GET_STR_FIELD(user, "identity_user");
    } else if (g_strcmp0(p->auth_method, "password") == 0) {
        GET_STR_FIELD(user, "password_user");
    } else {
        GET_STR_FIELD(user, "default_user");
    }

    GET_STR_FIELD(identity_file, "identity_file");

    GET_STR_FIELD(local_host, "local_host");
    p->local_port = form_get_int(app, "local_port", 0);

    GET_STR_FIELD(remote_bind_host, "remote_bind_host");
    p->remote_port = form_get_int(app, "remote_port", 0);

    GET_STR_FIELD(viewer_cmd, "viewer_cmd");
    GET_STR_FIELD(save_dir, "save_dir");
    GET_STR_FIELD(extra_ssh_args, "extra_ssh_args");

#undef GET_STR_FIELD

    if (str_empty(p->ssh_cmd)) {
        g_free(p->ssh_cmd);
        p->ssh_cmd = default_ssh_command();
    }

    if (str_empty(p->save_dir)) {
        g_free(p->save_dir);
        p->save_dir = g_strdup(g_get_tmp_dir());
    }

    if (app->form_cleanup_temp_files) {
        p->cleanup_temp_files = gtk_check_button_get_active(
            GTK_CHECK_BUTTON(app->form_cleanup_temp_files)
        );
    }

    password = g_strdup(entry_text(app->form_password_entry));
    if (!str_empty(password)) {
        g_free(p->password);
        p->password = g_strdup(password);
    } else if (app->editing_profile &&
               app->form_keep_password &&
               gtk_check_button_get_active(GTK_CHECK_BUTTON(app->form_keep_password))) {
        g_free(p->password);
        p->password = strdup0(app->editing_profile->password);
    }

    if (app->form_save_password) {
        save_password = gtk_check_button_get_active(GTK_CHECK_BUTTON(app->form_save_password));
    }

    g_free(p->backend);
    p->backend = g_strdup(gtk_string_object_get_string(
        gtk_drop_down_get_selected_item(GTK_DROP_DOWN(app->form_backend_combo))
    ));

    token = g_strdup(entry_text(app->form_token_entry));

    if (!str_empty(token)) {
        gchar *salt = make_salt();
        gchar *hash = sha256_hex_with_salt(salt, token);

        g_free(p->token_salt);
        g_free(p->token_hash);
        p->token_salt = salt;
        p->token_hash = hash;
    } else if (app->editing_profile &&
               gtk_check_button_get_active(GTK_CHECK_BUTTON(app->form_keep_token))) {
        g_free(p->token_salt);
        g_free(p->token_hash);
        p->token_salt = strdup0(app->editing_profile->token_salt);
        p->token_hash = strdup0(app->editing_profile->token_hash);
    }

    if (str_empty(p->name) || str_empty(p->host)) {
        app_log(app, "profile name or host is empty", "error");
        profile_free(p);
        g_free(password);
        g_free(token);
        return;
    }

    persisted = profile_copy(p);
    if (g_strcmp0(persisted->auth_method, "password") != 0 || !save_password) {
        g_free(persisted->password);
        persisted->password = g_strdup("");
    }

    profile_save_to_keyfile(app->config, persisted);
    profile_free(persisted);

    if (app_save_config(app)) {
        app_log(app, "profile saved successfully", "success");
    } else {
        app_log(app, "failed to save config", "error");
    }

    if (connect_after) {
        connect_profile(app, p);
    } else {
        show_start_screen(app);
    }

    profile_free(p);
    g_free(password);
    g_free(token);
}

static void on_save_profile_clicked(GtkButton *b, gpointer data)
{
    save_form_profile((AppState *)data, FALSE);
}

static void on_save_connect_clicked(GtkButton *b, gpointer data)
{
    save_form_profile((AppState *)data, TRUE);
}

static void on_cancel_form_clicked(GtkButton *b, gpointer data)
{
    show_start_screen((AppState *)data);
}

static gboolean app_profile_name_exists(AppState *app, const gchar *name)
{
    guint i;

    if (!app || !app->profiles || str_empty(name)) return FALSE;

    for (i = 0; i < app->profiles->len; i++) {
        Profile *p = g_ptr_array_index(app->profiles, i);
        if (p && g_strcmp0(p->name, name) == 0) return TRUE;
    }

    return FALSE;
}

static gchar *app_next_profile_name(AppState *app)
{
    guint i;

    for (i = 1; i < 10000; i++) {
        gchar *name = g_strdup_printf("New profile %u", i);
        if (!app_profile_name_exists(app, name)) return name;
        g_free(name);
    }

    return g_strdup("New profile");
}

static void show_profile_form(AppState *app, Profile *existing)
{
    Profile *base = existing ? profile_copy(existing) : profile_new_default();
    GtkWidget *title;
    GtkWidget *basic_grid;
    GtkWidget *auth_grid;
    GtkWidget *advanced_grid;
    GtkWidget *advanced_check;
    GtkWidget *btns;
    GtkStringList *backend_list;
    GtkStringList *auth_list;
    gchar *svg_temp_dir;
    guint selected = 0;
    guint i;
    int row;

    clear_main(app);

    if (!existing) {
        g_free(base->name);
        base->name = app_next_profile_name(app);
    }

    svg_temp_dir = g_strdup(str_empty(base->save_dir) ? g_get_tmp_dir() : base->save_dir);

    if (app->form_entries) {
        g_hash_table_destroy(app->form_entries);
    }

    app->form_entries = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    app->editing_profile = existing;

    title = gtk_label_new(existing ? "Edit Profile" : "New Profile");
    gtk_widget_add_css_class(title, "title-2");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(app->main_box), title);

    basic_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(basic_grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(basic_grid), 8);
    gtk_widget_set_hexpand(basic_grid, TRUE);

    form_add_key_entry(app, basic_grid, 0, "name", "Profile name", base->name, "New profile 1");
    form_add_key_entry(app, basic_grid, 1, "host", "Host", base->host, "example.com or 192.168.1.10");

    {
        gchar *s = g_strdup_printf("%d", base->ssh_port);
        form_add_key_entry(app, basic_grid, 2, "ssh_port", "Host port", s, "22");
        g_free(s);
    }

    gtk_box_append(GTK_BOX(app->main_box), basic_grid);

    {
        GtkWidget *section = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(section), "<b>Authentication</b>");
        gtk_widget_set_halign(section, GTK_ALIGN_START);
        gtk_widget_set_margin_top(section, 8);
        gtk_box_append(GTK_BOX(app->main_box), section);
    }

    auth_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(auth_grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(auth_grid), 8);
    gtk_widget_set_hexpand(auth_grid, TRUE);

    {
        GtkWidget *l = form_label_new("Verify with", TRUE);
        gtk_grid_attach(GTK_GRID(auth_grid), l, 0, 0, 1, 1);

        auth_list = gtk_string_list_new(NULL);
        for (i = 0; AUTH_OPTIONS[i].label; i++) {
            gtk_string_list_append(auth_list, AUTH_OPTIONS[i].label);
        }

        app->form_auth_combo = gtk_drop_down_new(G_LIST_MODEL(auth_list), NULL);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->form_auth_combo),
                                   auth_index_from_method(base->auth_method));
        gtk_grid_attach(GTK_GRID(auth_grid), app->form_auth_combo, 1, 0, 1, 1);
        g_signal_connect(app->form_auth_combo, "notify::selected",
                         G_CALLBACK(on_auth_selected_changed), app);
    }

    app->form_auth_default_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    {
        GtkWidget *default_grid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(default_grid), 6);
        gtk_grid_set_column_spacing(GTK_GRID(default_grid), 8);

        form_add_entry(app, default_grid, 0, "default_user",
                       "Username (optional)", base->user);

        {
            GtkWidget *hint = gtk_label_new("Leave empty to let OpenSSH use the current local login name.");
            gtk_widget_set_halign(hint, GTK_ALIGN_START);
            gtk_box_append(GTK_BOX(app->form_auth_default_box), default_grid);
            gtk_box_append(GTK_BOX(app->form_auth_default_box), hint);
        }
    }
    gtk_grid_attach(GTK_GRID(auth_grid), app->form_auth_default_box, 1, 1, 1, 1);

    app->form_auth_password_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    {
        GtkWidget *password_grid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(password_grid), 6);
        gtk_grid_set_column_spacing(GTK_GRID(password_grid), 8);

        form_add_key_entry(app, password_grid, 0, "password_user", "Username", base->user, "remote user");

        {
            GtkWidget *l = form_label_new("Password", TRUE);
            app->form_password_entry = gtk_password_entry_new();
            gtk_grid_attach(GTK_GRID(password_grid), l, 0, 1, 1, 1);
            gtk_grid_attach(GTK_GRID(password_grid), app->form_password_entry, 1, 1, 1, 1);
        }

        app->form_save_password = gtk_check_button_new_with_label("Save password in this profile");
        gtk_check_button_set_active(GTK_CHECK_BUTTON(app->form_save_password),
                                    !str_empty(base->password));

        app->form_keep_password = gtk_check_button_new_with_label("Keep saved password if password is empty");
        gtk_check_button_set_active(GTK_CHECK_BUTTON(app->form_keep_password),
                                    existing && !str_empty(existing->password));
        gtk_widget_set_visible(app->form_keep_password,
                               existing && !str_empty(existing->password));

        gtk_box_append(GTK_BOX(app->form_auth_password_box), password_grid);
        gtk_box_append(GTK_BOX(app->form_auth_password_box), app->form_save_password);
        gtk_box_append(GTK_BOX(app->form_auth_password_box), app->form_keep_password);
    }
    gtk_grid_attach(GTK_GRID(auth_grid), app->form_auth_password_box, 1, 2, 1, 1);

    app->form_auth_identity_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    {
        GtkWidget *identity_grid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(identity_grid), 6);
        gtk_grid_set_column_spacing(GTK_GRID(identity_grid), 8);

        form_add_browse_entry(app, identity_grid, 0, "identity_file", "Identity file", base->identity_file, FALSE);
        form_add_entry(app, identity_grid, 1, "identity_user", "Username", base->user);

        {
            GtkWidget *hint = gtk_label_new("Use ~/.ssh/id_ed25519 or another private key file.");
            gtk_widget_set_halign(hint, GTK_ALIGN_START);
            gtk_box_append(GTK_BOX(app->form_auth_identity_box), hint);
        }
        gtk_box_append(GTK_BOX(app->form_auth_identity_box), identity_grid);
    }
    gtk_grid_attach(GTK_GRID(auth_grid), app->form_auth_identity_box, 1, 3, 1, 1);

    gtk_box_append(GTK_BOX(app->main_box), auth_grid);
    update_auth_visibility(app);

    advanced_check = gtk_check_button_new_with_label("Advanced");
    gtk_widget_set_margin_top(advanced_check, 8);
    g_signal_connect(advanced_check, "toggled", G_CALLBACK(on_advanced_toggled), app);
    gtk_box_append(GTK_BOX(app->main_box), advanced_check);

    app->form_advanced_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_visible(app->form_advanced_box, FALSE);

    advanced_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(advanced_grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(advanced_grid), 8);
    gtk_widget_set_hexpand(advanced_grid, TRUE);

    row = 0;
    form_add_browse_entry(app, advanced_grid, row++, "ssh_cmd", "SSH executable", base->ssh_cmd, FALSE);

    form_add_entry(app, advanced_grid, row++, "local_host", "Local listen address", base->local_host);

    {
        gchar *s = g_strdup_printf("%d", base->local_port);
        form_add_entry(app, advanced_grid, row++, "local_port", "Local listen port (0=random)", s);
        g_free(s);
    }

    form_add_entry(app, advanced_grid, row++, "remote_bind_host", "Remote bind address", base->remote_bind_host);

    {
        gchar *s = g_strdup_printf("%d", base->remote_port);
        form_add_entry(app, advanced_grid, row++, "remote_port", "Remote reverse port (0=random)", s);
        g_free(s);
    }

    form_add_entry(app, advanced_grid, row++, "viewer_cmd", "SVG viewer command", base->viewer_cmd);

    {
        GtkWidget *l = gtk_label_new("SVG viewer backend");
        gtk_widget_set_halign(l, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(advanced_grid), l, 0, row, 1, 1);

        backend_list = gtk_string_list_new((const char *[]){"gtk", "x11", "auto", "none", NULL});
        if (g_strcmp0(base->backend, "x11") == 0) selected = 1;
        else if (g_strcmp0(base->backend, "auto") == 0) selected = 2;
        else if (g_strcmp0(base->backend, "none") == 0) selected = 3;
        else selected = 0;

        app->form_backend_combo = gtk_drop_down_new(G_LIST_MODEL(backend_list), NULL);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->form_backend_combo), selected);
        gtk_grid_attach(GTK_GRID(advanced_grid), app->form_backend_combo, 1, row, 1, 1);
        row++;
    }

    form_add_browse_entry(app, advanced_grid, row++, "save_dir", "SVG temp folder", svg_temp_dir, TRUE);
    app->form_cleanup_temp_files = gtk_check_button_new_with_label("Clean up SVG files after viewer closes");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(app->form_cleanup_temp_files),
                                base->cleanup_temp_files);
    gtk_grid_attach(GTK_GRID(advanced_grid), app->form_cleanup_temp_files, 1, row++, 1, 1);

    form_add_entry(app, advanced_grid, row++, "extra_ssh_args", "Extra SSH arguments", base->extra_ssh_args);

    app->form_keep_token = gtk_check_button_new_with_label("Keep existing token if new token is empty");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(app->form_keep_token),
                                existing && !str_empty(existing->token_hash));

    {
        GtkWidget *label = gtk_label_new("New transfer token, optional");
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(app->form_advanced_box), advanced_grid);
        gtk_box_append(GTK_BOX(app->form_advanced_box), app->form_keep_token);
        gtk_box_append(GTK_BOX(app->form_advanced_box), label);
    }

    app->form_token_entry = gtk_password_entry_new();
    gtk_box_append(GTK_BOX(app->form_advanced_box), app->form_token_entry);

    {
        GtkWidget *hint = gtk_label_new("SSH password and key passphrase prompts are handled by the rsfclient GUI. Saved passwords are stored in the rsfclient profile file.");
        gtk_widget_set_halign(hint, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(app->form_advanced_box), hint);
    }

    gtk_box_append(GTK_BOX(app->main_box), app->form_advanced_box);

    btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(btns), make_button("Save", G_CALLBACK(on_save_profile_clicked), app));
    gtk_box_append(GTK_BOX(btns), make_green_button("Save && Connect", G_CALLBACK(on_save_connect_clicked), app));
    gtk_box_append(GTK_BOX(btns), make_button("Cancel", G_CALLBACK(on_cancel_form_clicked), app));
    gtk_box_append(GTK_BOX(app->main_box), btns);

    g_free(svg_temp_dir);
    profile_free(base);
}

/* ---------------------------------------------------------------------- */
/* Application                                                             */
/* ---------------------------------------------------------------------- */

static void app_activate(GtkApplication *gtk_app, gpointer user_data)
{
    AppState *app = (AppState *)user_data;

    app->gtk_app = gtk_app;
    install_app_css();

    app->window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(GTK_WINDOW(app->window), "RSF View Client");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 860, 560);

    app->main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(app->main_box, 10);
    gtk_widget_set_margin_bottom(app->main_box, 10);
    gtk_widget_set_margin_start(app->main_box, 10);
    gtk_widget_set_margin_end(app->main_box, 10);

    gtk_window_set_child(GTK_WINDOW(app->window), app->main_box);

    show_start_screen(app);

    gtk_window_present(GTK_WINDOW(app->window));
}

static void app_destroy(AppState *app)
{
    if (!app) return;

    if (app->runtime) {
        runtime_stop(app->runtime);
        app->runtime = NULL;
    }

    if (app->profiles) g_ptr_array_unref(app->profiles);
    if (app->config) g_key_file_unref(app->config);
    if (app->form_entries) g_hash_table_destroy(app->form_entries);

    g_free(app->config_path);
    g_free(app);
}

static int gui_main(int argc, char **argv)
{
    GtkApplication *gtk_app;
    AppState *state;
    int status;

    state = g_new0(AppState, 1);
    app_load_config(state);
    app_reload_profiles(state);

    gtk_app = gtk_application_new("org.rsfpy.rsfclient", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(gtk_app, "activate", G_CALLBACK(app_activate), state);

    status = g_application_run(G_APPLICATION(gtk_app), argc, argv);

    g_object_unref(gtk_app);
    app_destroy(state);

    return status;
}

static gboolean argv_has(int argc, char **argv, const gchar *opt)
{
    int i;
    for (i = 1; i < argc; i++) {
        if (g_strcmp0(argv[i], opt) == 0) return TRUE;
    }
    return FALSE;
}

int main(int argc, char **argv)
{
    self_exe_path = resolve_executable_path(argc > 0 ? argv[0] : NULL);

#ifdef G_OS_WIN32
    setup_windows_bundle_env(self_exe_path);
#endif

    if (g_strcmp0(g_getenv("RSFCLIENT_ASKPASS"), "1") == 0 ||
        argv_has(argc, argv, "--askpass")) {
        return askpass_main(argc, argv);
    }

    if (argv_has(argc, argv, "--version")) {
        g_print("%s\n", APP_VERSION);
        return 0;
    }

    if (argv_has(argc, argv, "--send")) {
        return sender_main(argc, argv);
    }

    return gui_main(argc, argv);
}
