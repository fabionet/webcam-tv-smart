// WebCam and TV Smart Vision — punto d'ingresso.
#include <gst/gst.h>
#include <gtk/gtk.h>

#include <memory>

#include "app_window.h"

namespace {
std::unique_ptr<AppWindow> g_window;
std::string g_selfTestDir;

void onActivate(GtkApplication* app, gpointer) {
    if (g_window) {
        gtk_window_present(GTK_WINDOW(g_window->widget()));
        return;
    }
    g_window = std::make_unique<AppWindow>(app);
    g_signal_connect(g_window->widget(), "destroy", G_CALLBACK(+[](GtkWidget*, gpointer) { g_window.reset(); }), nullptr);
    if (!g_selfTestDir.empty()) g_window->runSelfTest(g_selfTestDir);
}
} // namespace

int main(int argc, char** argv) {
    gst_init(&argc, &argv);
    // --self-test [cartella]: verifica automatica di sorgente, registrazione MP4/OGG e istantanea.
    for (int i = 1; i < argc; ++i) {
        if (g_strcmp0(argv[i], "--self-test") == 0) {
            g_selfTestDir = (i + 1 < argc) ? argv[i + 1] : g_get_tmp_dir();
            for (int j = i; j < argc; ++j) argv[j] = nullptr;
            argc = i;
            break;
        }
        if (g_strcmp0(argv[i], "--version") == 0) {
            g_print("WebCam and TV Smart Vision %s\n", APP_VERSION);
            return 0;
        }
    }
    g_set_prgname("webcam-tv-smart-vision");
    g_set_application_name("WebCam and TV Smart Vision");
    GtkApplication* app = gtk_application_new(APP_ID, G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(onActivate), nullptr);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_window.reset();
    g_object_unref(app);
    return status;
}
