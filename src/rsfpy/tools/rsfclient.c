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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_VERSION "gtk-client-v4-cancel-2026-06-19"
#define MAGIC "RSFVIEW2"
#define MAGIC_LEN 8
#define DEFAULT_PORT 17890
#define MAX_PAYLOAD ((guint64)1024 * 1024 * 1024)
#define MAX_RETRIES 3
#define RETRY_DELAY_SEC 3

typedef struct _Profile Profile;
typedef struct _Runtime Runtime;
typedef struct _AppState AppState;

struct _Profile {
    gchar *name;
    gchar *ssh_cmd;
    gchar *user;
    gchar *host;
    gint ssh_port;

    gchar *local_host;
    gint local_port;

    gchar *remote_bind_host;
    gint remote_port;

    gchar *viewer_cmd;
    gchar *backend;
    gchar *save_dir;
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
    GSubprocess *ssh_proc;
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
    GtkWidget *form_keep_token;
    GtkWidget *form_token_entry;
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
    p->ssh_cmd = g_strdup("ssh");
    p->user = g_strdup(g_get_user_name() ? g_get_user_name() : "");
    p->host = g_strdup("");
    p->ssh_port = 22;

    p->local_host = g_strdup("127.0.0.1");
    p->local_port = DEFAULT_PORT;

    p->remote_bind_host = g_strdup("127.0.0.1");
    p->remote_port = DEFAULT_PORT;

    p->viewer_cmd = g_strdup("svgviewer");
    p->backend = g_strdup("gtk");
    p->save_dir = g_strdup("");
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

    CP_STR(local_host);
    p->local_port = src->local_port;

    CP_STR(remote_bind_host);
    p->remote_port = src->remote_port;

    CP_STR(viewer_cmd);
    CP_STR(backend);
    CP_STR(save_dir);
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

    return g_strdup_printf(
        "%s  [%s@%s:%d  R:%d -> L:%d  %s]",
        p->name ? p->name : "unnamed",
        p->user ? p->user : "",
        p->host ? p->host : "",
        p->ssh_port,
        p->remote_port,
        p->local_port,
        p->backend ? p->backend : "gtk"
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

static Profile *profile_load_from_group(GKeyFile *kf, const gchar *group)
{
    Profile *p = profile_new_default();
    const gchar *name = group;

    if (g_str_has_prefix(group, "profile:")) {
        name = group + strlen("profile:");
    }

#define SET_STR(field, key, def) do { \
        g_free(p->field); \
        p->field = key_get_string_default(kf, group, key, def); \
    } while (0)

    g_free(p->name);
    p->name = g_strdup(name);

    SET_STR(ssh_cmd, "ssh_cmd", "ssh");
    SET_STR(user, "user", g_get_user_name() ? g_get_user_name() : "");
    SET_STR(host, "host", "");
    p->ssh_port = key_get_int_default(kf, group, "ssh_port", 22);

    SET_STR(local_host, "local_host", "127.0.0.1");
    p->local_port = key_get_int_default(kf, group, "local_port", DEFAULT_PORT);

    SET_STR(remote_bind_host, "remote_bind_host", "127.0.0.1");
    p->remote_port = key_get_int_default(kf, group, "remote_port", DEFAULT_PORT);

    SET_STR(viewer_cmd, "viewer_cmd", "svgviewer");
    SET_STR(backend, "backend", "gtk");
    SET_STR(save_dir, "save_dir", "");
    SET_STR(extra_ssh_args, "extra_ssh_args", "");

    SET_STR(token_salt, "token_salt", "");
    SET_STR(token_hash, "token_hash", "");

#undef SET_STR

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

    SET_STR("local_host", p->local_host);
    g_key_file_set_integer(kf, group, "local_port", p->local_port);

    SET_STR("remote_bind_host", p->remote_bind_host);
    g_key_file_set_integer(kf, group, "remote_port", p->remote_port);

    SET_STR("viewer_cmd", p->viewer_cmd);
    SET_STR("backend", p->backend);
    SET_STR("save_dir", p->save_dir);
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
    const gchar *host = "127.0.0.1";
    const gchar *token = g_getenv("RSFVIEW_TOKEN");
    const gchar *title = "";
    gint port = DEFAULT_PORT;
    GByteArray *payload = NULL;
    GError *err = NULL;
    int i;

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
            g_print("Usage: rsfclient --send [--host 127.0.0.1] [--port 17890] [--token TOKEN] [--title TITLE] < figure.svg\n");
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
/* Receiver protocol                                                       */
/* ---------------------------------------------------------------------- */

static gboolean input_read_exact(GInputStream *in, void *buf, gsize len, GError **err)
{
    gsize done = 0;
    return g_input_stream_read_all(in, buf, len, &done, NULL, err);
}

static gchar *extract_title_from_meta(const gchar *meta)
{
    const gchar *p;
    const gchar *colon;
    const gchar *q;
    GString *out;

    if (str_empty(meta)) return g_strdup("figure");

    p = g_strstr_len(meta, -1, "\"title\"");
    if (!p) return g_strdup("figure");

    colon = strchr(p, ':');
    if (!colon) return g_strdup("figure");

    q = strchr(colon, '"');
    if (!q) return g_strdup("figure");
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
        g_string_append(out, "figure");
    }

    return g_string_free(out, FALSE);
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
    gchar *filename = g_strdup_printf("%s_%s.svg", safe, stamp);
    gchar *base = NULL;
    gchar *path;

    if (p && !str_empty(p->save_dir)) {
        base = g_strdup(p->save_dir);
        *temporary = FALSE;
    } else {
        base = g_strdup(g_get_tmp_dir());
        *temporary = TRUE;
    }

    g_mkdir_with_parents(base, 0700);
    path = g_build_filename(base, filename, NULL);

    g_free(safe);
    g_free(stamp);
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
    gchar *path;
} ChildDeleteData;

static void viewer_child_exit_cb(GPid pid, gint status, gpointer user_data)
{
    ChildDeleteData *d = (ChildDeleteData *)user_data;

    if (d && d->path) {
        g_remove(d->path);
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

    if (!str_empty(p->backend) && g_strcmp0(p->backend, "none") != 0) {
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

    if (!g_spawn_async(NULL, argv, NULL,
                       G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                       NULL, NULL, &pid, &err)) {
        app_log(job->rt->app, err ? err->message : "failed to open viewer", "error");
        if (err) g_error_free(err);
        goto done;
    }

    if (job->temporary) {
        ChildDeleteData *d = g_new0(ChildDeleteData, 1);
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

static gchar **build_ssh_argv(Profile *p)
{
    GPtrArray *arr = g_ptr_array_new_with_free_func(g_free);
    gchar *target;
    gchar *forward;
    gchar *port_s;
    gchar **extra = NULL;
    int i;

    g_ptr_array_add(arr, g_strdup(str_empty(p->ssh_cmd) ? "ssh" : p->ssh_cmd));
    g_ptr_array_add(arr, g_strdup("-N"));
    g_ptr_array_add(arr, g_strdup("-p"));

    port_s = g_strdup_printf("%d", p->ssh_port);
    g_ptr_array_add(arr, port_s);

    g_ptr_array_add(arr, g_strdup("-o"));
    g_ptr_array_add(arr, g_strdup("ExitOnForwardFailure=yes"));
    g_ptr_array_add(arr, g_strdup("-o"));
    g_ptr_array_add(arr, g_strdup("ServerAliveInterval=30"));
    g_ptr_array_add(arr, g_strdup("-o"));
    g_ptr_array_add(arr, g_strdup("ServerAliveCountMax=3"));

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
    g_ptr_array_add(arr, NULL);

    return (gchar **)g_ptr_array_free(arr, FALSE);
}

typedef struct {
    GSubprocess *proc;
    GMutex mutex;
    GCond cond;
    gboolean exited;
    gint exit_code;
} WaitState;

static gpointer subprocess_wait_thread(gpointer data)
{
    WaitState *ws = (WaitState *)data;
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

    return NULL;
}

static gpointer ssh_output_thread(gpointer data)
{
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
        gchar *msg = g_strdup_printf("ssh: %s", line);
        app_log(rt->app, msg, is_error_text(line) ? "error" : "info");
        g_free(msg);
        g_free(line);
    }

    if (err) g_error_free(err);
    g_object_unref(din);

    return NULL;
}

static gpointer tunnel_thread_func(gpointer data)
{
    Runtime *rt = (Runtime *)data;
    int retries = 0;

    while (!runtime_is_stopping(rt)) {
        gchar **argv = build_ssh_argv(rt->profile);
        gchar *cmdline = g_strjoinv(" ", argv);
        GError *err = NULL;
        WaitState ws;
        GThread *wait_thread;
        gboolean early_exit;
        gint64 until;

        {
            gchar *msg = g_strdup_printf("starting ssh tunnel: %s", cmdline);
            app_log(rt->app, msg, "info");
            g_free(msg);
        }

        rt->ssh_proc = g_subprocess_newv((const gchar * const *)argv,
                                         G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                         G_SUBPROCESS_FLAGS_STDERR_MERGE,
                                         &err);

        g_free(cmdline);
        g_strfreev(argv);

        if (!rt->ssh_proc) {
            retries++;

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

        g_thread_new("ssh-output", ssh_output_thread, rt);

        g_mutex_init(&ws.mutex);
        g_cond_init(&ws.cond);
        ws.proc = rt->ssh_proc;
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
            app_log(rt->app, "ssh tunnel established successfully", "success");
        }

        g_mutex_lock(&ws.mutex);
        while (!ws.exited) {
            g_cond_wait(&ws.cond, &ws.mutex);
        }
        g_mutex_unlock(&ws.mutex);

        g_thread_join(wait_thread);

        if (runtime_is_stopping(rt)) {
            g_object_unref(rt->ssh_proc);
            rt->ssh_proc = NULL;
            g_mutex_clear(&ws.mutex);
            g_cond_clear(&ws.cond);
            break;
        }

        {
            gchar *msg = g_strdup_printf("ssh tunnel disconnected with code %d", ws.exit_code);
            app_log(rt->app, msg, "error");
            g_free(msg);
        }

        g_object_unref(rt->ssh_proc);
        rt->ssh_proc = NULL;

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

    rt->app = app;
    rt->profile = profile_copy(profile);
    rt->listener = NULL;
    rt->cancel = g_cancellable_new();
    rt->ssh_proc = NULL;
    rt->stopping = FALSE;
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

    if (rt->ssh_proc) {
        g_subprocess_force_exit(rt->ssh_proc);
    }
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
    if (rt->ssh_proc) g_object_unref(rt->ssh_proc);

    profile_free(rt->profile);
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

static void clear_main(AppState *app)
{
    GtkWidget *child;

    /*
       The log view belongs to the current screen.  After removing the screen,
       background runtime threads must not keep appending to the old TextBuffer.
    */
    app->log_view = NULL;
    app->log_buffer = NULL;

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

static void form_add_entry(AppState *app, GtkWidget *grid, int row,
                           const gchar *key, const gchar *label,
                           const gchar *value)
{
    GtkWidget *l = gtk_label_new(label);
    GtkWidget *e = entry_new_text(value);

    gtk_widget_set_halign(l, GTK_ALIGN_START);
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

static void connect_profile(AppState *app, Profile *p)
{
    gchar *summary;

    if (!p) return;

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

static void on_delete_profile_clicked(GtkButton *b, gpointer data)
{
    AppState *app = (AppState *)data;
    gboolean ok;
    guint idx = get_selected_profile_index(app, &ok);
    Profile *p;
    gchar *group;

    if (!ok) return;

    p = app_get_profile_by_index(app, idx);
    if (!p) return;

    group = profile_group_name(p->name);
    g_key_file_remove_group(app->config, group, NULL);
    g_free(group);

    app_save_config(app);
    show_start_screen(app);
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

        gtk_box_append(GTK_BOX(box), make_button("Connect", G_CALLBACK(on_connect_clicked), app));
        gtk_box_append(GTK_BOX(box), make_button("New Profile", G_CALLBACK(on_new_profile_clicked), app));
        gtk_box_append(GTK_BOX(box), make_button("Edit Profile", G_CALLBACK(on_edit_profile_clicked), app));
        gtk_box_append(GTK_BOX(box), make_button("Delete Profile", G_CALLBACK(on_delete_profile_clicked), app));
        gtk_box_append(GTK_BOX(box), make_button("Exit", G_CALLBACK(on_exit_clicked), app));

        gtk_box_append(GTK_BOX(app->main_box), box);
    }
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
    gtk_text_buffer_create_tag(app->log_buffer, "info", NULL);

    sw = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(sw, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), app->log_view);
    gtk_box_append(GTK_BOX(app->main_box), sw);
}

static void save_form_profile(AppState *app, gboolean connect_after)
{
    Profile *p = profile_new_default();
    gchar *token;

#define GET_STR_FIELD(field, key) do { g_free(p->field); p->field = form_get(app, key); } while (0)

    GET_STR_FIELD(name, "name");
    GET_STR_FIELD(ssh_cmd, "ssh_cmd");
    GET_STR_FIELD(user, "user");
    GET_STR_FIELD(host, "host");
    p->ssh_port = form_get_int(app, "ssh_port", 22);

    GET_STR_FIELD(local_host, "local_host");
    p->local_port = form_get_int(app, "local_port", DEFAULT_PORT);

    GET_STR_FIELD(remote_bind_host, "remote_bind_host");
    p->remote_port = form_get_int(app, "remote_port", DEFAULT_PORT);

    GET_STR_FIELD(viewer_cmd, "viewer_cmd");
    GET_STR_FIELD(save_dir, "save_dir");
    GET_STR_FIELD(extra_ssh_args, "extra_ssh_args");

#undef GET_STR_FIELD

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
        g_free(token);
        return;
    }

    profile_save_to_keyfile(app->config, p);

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

static void show_profile_form(AppState *app, Profile *existing)
{
    Profile *base = existing ? profile_copy(existing) : profile_new_default();
    GtkWidget *title;
    GtkWidget *grid;
    GtkWidget *btns;
    GtkStringList *backend_list;
    guint selected = 0;

    clear_main(app);

    if (app->form_entries) {
        g_hash_table_destroy(app->form_entries);
    }

    app->form_entries = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    app->editing_profile = existing;

    title = gtk_label_new(existing ? "Edit Profile" : "New Profile");
    gtk_widget_add_css_class(title, "title-2");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(app->main_box), title);

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_widget_set_hexpand(grid, TRUE);

    form_add_entry(app, grid, 0, "name", "Profile name", base->name);
    form_add_entry(app, grid, 1, "ssh_cmd", "SSH command path", base->ssh_cmd);
    form_add_entry(app, grid, 2, "user", "SSH username", base->user);
    form_add_entry(app, grid, 3, "host", "SSH host", base->host);

    {
        gchar *s = g_strdup_printf("%d", base->ssh_port);
        form_add_entry(app, grid, 4, "ssh_port", "SSH port", s);
        g_free(s);
    }

    form_add_entry(app, grid, 5, "local_host", "Local listen host", base->local_host);

    {
        gchar *s = g_strdup_printf("%d", base->local_port);
        form_add_entry(app, grid, 6, "local_port", "Local listen port", s);
        g_free(s);
    }

    form_add_entry(app, grid, 7, "remote_bind_host", "Remote bind host", base->remote_bind_host);

    {
        gchar *s = g_strdup_printf("%d", base->remote_port);
        form_add_entry(app, grid, 8, "remote_port", "Remote reverse port", s);
        g_free(s);
    }

    form_add_entry(app, grid, 9, "viewer_cmd", "Viewer command", base->viewer_cmd);

    {
        GtkWidget *l = gtk_label_new("Viewer backend");
        gtk_widget_set_halign(l, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(grid), l, 0, 10, 1, 1);

        backend_list = gtk_string_list_new((const char *[]){"gtk", "x11", "auto", "none", NULL});
        if (g_strcmp0(base->backend, "x11") == 0) selected = 1;
        else if (g_strcmp0(base->backend, "auto") == 0) selected = 2;
        else if (g_strcmp0(base->backend, "none") == 0) selected = 3;
        else selected = 0;

        app->form_backend_combo = gtk_drop_down_new(G_LIST_MODEL(backend_list), NULL);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->form_backend_combo), selected);
        gtk_grid_attach(GTK_GRID(grid), app->form_backend_combo, 1, 10, 1, 1);
    }

    form_add_entry(app, grid, 11, "save_dir", "Save SVG dir", base->save_dir);
    form_add_entry(app, grid, 12, "extra_ssh_args", "Extra ssh args", base->extra_ssh_args);

    gtk_box_append(GTK_BOX(app->main_box), grid);

    app->form_keep_token = gtk_check_button_new_with_label("Keep existing token if new token is empty");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(app->form_keep_token),
                                existing && !str_empty(existing->token_hash));
    gtk_box_append(GTK_BOX(app->main_box), app->form_keep_token);

    {
        GtkWidget *label = gtk_label_new("New transfer token, optional");
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(app->main_box), label);
    }

    app->form_token_entry = gtk_password_entry_new();
    gtk_box_append(GTK_BOX(app->main_box), app->form_token_entry);

    {
        GtkWidget *hint = gtk_label_new("SSH password is not saved. Authentication is handled by system ssh / keys / ssh-agent.");
        gtk_widget_set_halign(hint, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(app->main_box), hint);
    }

    btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(btns), make_button("Save", G_CALLBACK(on_save_profile_clicked), app));
    gtk_box_append(GTK_BOX(btns), make_button("Save && Connect", G_CALLBACK(on_save_connect_clicked), app));
    gtk_box_append(GTK_BOX(btns), make_button("Cancel", G_CALLBACK(on_cancel_form_clicked), app));
    gtk_box_append(GTK_BOX(app->main_box), btns);

    profile_free(base);
}

/* ---------------------------------------------------------------------- */
/* Application                                                             */
/* ---------------------------------------------------------------------- */

static void app_activate(GtkApplication *gtk_app, gpointer user_data)
{
    AppState *app = (AppState *)user_data;

    app->gtk_app = gtk_app;
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

    gtk_app = gtk_application_new("org.rsfpy.rsfclient", G_APPLICATION_DEFAULT_FLAGS);
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
    if (argv_has(argc, argv, "--version")) {
        g_print("%s\n", APP_VERSION);
        return 0;
    }

    if (argv_has(argc, argv, "--send")) {
        return sender_main(argc, argv);
    }

    return gui_main(argc, argv);
}
