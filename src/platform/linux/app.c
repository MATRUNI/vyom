#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "internal.h"
#include "bridge.h"
#include "memi.h"
#include "ipc.h"
#include "process.h"

GtkWidget *g_active_webview = NULL;
GtkWidget *g_active_window = NULL;

static void on_uri_scheme_request(WebKitURISchemeRequest *request, gpointer user_data)
{
    const char *root_dir = (const char *)user_data; // "./dist"
    const char *path = webkit_uri_scheme_request_get_path(request); // e.g. "/assets/main.js"
    if (!path || strcmp(path, "/") == 0 || *path == '\0')
        path = "/index.html";

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s%s", root_dir, path);

    GFile *file = g_file_new_for_path(full_path);
    GFileInputStream *stream = g_file_read(file, NULL, NULL);

    if (!stream)
    {
        g_object_unref(file);
        snprintf(full_path, sizeof(full_path), "%s/index.html", root_dir);
        file = g_file_new_for_path(full_path);
        stream = g_file_read(file, NULL, NULL);
        if (!stream)
        {
            GError *err = g_error_new(G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Not Found");
            webkit_uri_scheme_request_finish_error(request, err);
            g_error_free(err);
            g_object_unref(file);
            return;
        }
    }

    struct stat st;
    stat(full_path, &st);
    gint64 total_size = (gint64)st.st_size;
    const char *mime = get_memi_type(full_path);

    // Check for HTTP Range header (crucial for 4K/8K video streaming & seeking)
    SoupMessageHeaders *req_headers = webkit_uri_scheme_request_get_http_headers(request);
    const char *range_header = req_headers ? soup_message_headers_get_one(req_headers, "Range") : NULL;
    if (range_header && g_str_has_prefix(range_header, "bytes="))
    {
        gint64 start = 0, end = total_size - 1;
        const char *range_val = range_header + 6; // skip "bytes="
        char **parts = g_strsplit(range_val, "-", 2);
        if (parts[0] && parts[0][0] != '\0')
            start = g_ascii_strtoll(parts[0], NULL, 10);
        if (parts[1] && parts[1][0] != '\0')
            end = g_ascii_strtoll(parts[1], NULL, 10);
        g_strfreev(parts);

        if (start >= total_size)
        {
            GError *err = g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Requested range not satisfiable");
            webkit_uri_scheme_request_finish_error(request, err);
            g_error_free(err);
            g_object_unref(stream);
            g_object_unref(file);
            return;
        }
        if (end >= total_size)
            end = total_size - 1;

        gint64 content_length = (end - start) + 1;
        if (G_IS_SEEKABLE(stream))
            g_seekable_seek(G_SEEKABLE(stream), start, G_SEEK_SET, NULL, NULL);

        WebKitURISchemeResponse *response = webkit_uri_scheme_response_new(G_INPUT_STREAM(stream), content_length);
        webkit_uri_scheme_response_set_status(response, 206, "Partial Content");
        webkit_uri_scheme_response_set_content_type(response, mime);
        SoupMessageHeaders *resp_headers = soup_message_headers_new(SOUP_MESSAGE_HEADERS_RESPONSE);
        char content_range[128];
        snprintf(content_range, sizeof(content_range), "bytes %" G_GINT64_FORMAT "-%" G_GINT64_FORMAT "/%" G_GINT64_FORMAT,
                 start, end, total_size);
        soup_message_headers_append(resp_headers, "Accept-Ranges", "bytes");
        soup_message_headers_append(resp_headers, "Content-Range", content_range);
        // WebKit adopts resp_headers (do NOT call soup_message_headers_unref!)
        webkit_uri_scheme_response_set_http_headers(response, resp_headers);
        webkit_uri_scheme_request_finish_with_response(request, response);
        g_object_unref(response);
    }
    else
    {
        webkit_uri_scheme_request_finish(request, G_INPUT_STREAM(stream), total_size, mime);
    }

    g_object_unref(stream);
    g_object_unref(file);
}

static void on_load_changed(WebKitWebView *webview, WebKitLoadEvent load_event, gpointer user_data)
{
    (void)webview; (void)user_data;
    if (load_event == WEBKIT_LOAD_STARTED)
        process_cleanup_all();
}

static void activate_callback(GtkApplication *app, gpointer user_data)
{
    app_config_t *con = (app_config_t *)user_data;

    GtkWidget *window = gtk_application_window_new(app);
    g_active_window = window;
    gtk_window_set_title(GTK_WINDOW(window), con->window->title);
    gtk_window_set_default_size(GTK_WINDOW(window), con->window->width, con->window->height);

    WebKitWebContext *context = webkit_web_context_get_default();
    WebKitSecurityManager *sm = webkit_web_context_get_security_manager(context);
    webkit_security_manager_register_uri_scheme_as_local(sm, "app");
    webkit_security_manager_register_uri_scheme_as_cors_enabled(sm, "app");
    webkit_security_manager_register_uri_scheme_as_secure(sm, "app");
    webkit_web_context_register_uri_scheme(context, "app", on_uri_scheme_request, "./dist", NULL);

    GtkWidget *webview = webkit_web_view_new();
    g_active_webview = webview;
    g_signal_connect(webview, "load-changed", G_CALLBACK(on_load_changed), NULL);
    gtk_widget_set_hexpand(webview, TRUE);
    gtk_widget_set_vexpand(webview, TRUE);

    WebKitSettings *settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(webview));
    webkit_settings_set_disable_web_security(settings, con->webview->disable_web_security);
    webkit_settings_set_enable_media(settings, TRUE);
    webkit_settings_set_media_playback_requires_user_gesture(settings, FALSE);
    webkit_settings_set_media_playback_allows_inline(settings, TRUE);
    if (con->webview->developer_extras)
    {
        webkit_settings_set_enable_developer_extras(settings, con->webview->developer_extras);
        WebKitWebInspector *inspector = webkit_web_view_get_inspector(WEBKIT_WEB_VIEW(webview));
        webkit_web_inspector_show(inspector);
    }

    gtk_window_set_resizable(GTK_WINDOW(window), con->window->resizable);
    gtk_widget_set_size_request(window, con->window->min_width, con->window->min_height);
    gtk_window_set_decorated(GTK_WINDOW(window), con->window->decorations);
    gtk_window_set_child(GTK_WINDOW(window), webview);

    if (con->window->maximized) gtk_window_maximize(GTK_WINDOW(window));
    if (con->window->fullscreen) gtk_window_fullscreen(GTK_WINDOW(window));
    if (con->window->icon_name) gtk_window_set_icon_name(GTK_WINDOW(window), con->window->icon_name);

    if (con->webview->user_agent)
        webkit_settings_set_user_agent(settings, con->webview->user_agent);

    webkit_settings_set_enable_javascript(settings, con->webview->javascript_enabled);
    webkit_settings_set_auto_load_images(settings, con->webview->auto_load_images);
    webkit_settings_set_javascript_can_open_windows_automatically(settings, con->webview->javascript_can_open_windows);

    WebKitUserContentManager *manager = webkit_web_view_get_user_content_manager(WEBKIT_WEB_VIEW(webview));
    ipc_init(manager);

    WebKitUserScript *bridge_script = webkit_user_script_new(
        bridge_js,
        WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
        NULL, NULL
    );
    webkit_user_content_manager_add_script(manager, bridge_script);
    webkit_user_script_unref(bridge_script);

    if (con->webview->startup_script)
    {
        WebKitUserScript *script = webkit_user_script_new(
            con->webview->startup_script,
            WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
            WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
            NULL, NULL
        );
        webkit_user_content_manager_add_script(manager, script);
        webkit_user_script_unref(script);
    }

    if (con->webview->custom_css)
    {
        WebKitUserStyleSheet *style = webkit_user_style_sheet_new(
            con->webview->custom_css,
            WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
            WEBKIT_USER_STYLE_LEVEL_USER,
            NULL, NULL
        );
        webkit_user_content_manager_add_style_sheet(manager, style);
        webkit_user_style_sheet_unref(style);
    }

    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(webview), con->webview->url);
    gtk_window_present(GTK_WINDOW(window));
}

int platform_app_run(const app_config_t *config, int argc, char *argv[])
{
    GtkApplication *app = gtk_application_new(NULL, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate_callback), (gpointer)config);

    g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return 0;
}

void platform_app_emit(const char *event, const char *json_data)
{
    if (!g_active_webview || !event) return;

    const char *payload = (json_data && *json_data) ? json_data : "null";

    char *script = g_strdup_printf("window.vyom._emit(\"%s\", %s);", event, payload);
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(g_active_webview), script, -1, NULL, NULL, NULL, NULL, NULL);
    g_free(script);
}
