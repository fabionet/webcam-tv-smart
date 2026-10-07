#include "app_window.h"

#include <gio/gio.h>

#include <algorithm>
#include <cmath>
#include <ctime>

#define SELF static_cast<AppWindow*>(self)

namespace {
const char* APP_TITLE = "WebCam and TV Smart Vision";

const char* CSS =
    ".video-area { background-color: #000000; }"
    ".rec-badge { background-color: #e01b24; color: white; font-weight: bold; padding: 3px 10px; border-radius: 6px; margin: 8px; }"
    ".video-info { background-color: rgba(0,0,0,0.55); color: white; padding: 2px 8px; border-radius: 4px; margin: 8px; font-size: 90%; }"
    ".page { padding: 10px; }"
    ".heading { font-weight: bold; margin-top: 6px; }"
    ".dim { opacity: 0.7; font-size: 90%; }"
    ".status { padding: 3px 8px; }"
    ".tab-label { font-size: 80%; }"
    ".placeholder-bg { background-color: #000000; }"
    ".placeholder { color: #d0d0d0; padding: 18px 24px; border-radius: 12px; background-color: rgba(255,255,255,0.06); }"
    ".placeholder-title { font-size: 125%; font-weight: bold; color: white; }"
    ".device-bar { padding: 6px 8px; border-radius: 8px; background-color: alpha(@theme_fg_color, 0.06); }"
    ".device-name { font-weight: bold; }";

std::string timestamp() {
    char buf[32];
    std::time_t t = std::time(nullptr);
    std::strftime(buf, sizeof buf, "%Y%m%d-%H%M%S", std::localtime(&t));
    return buf;
}

std::string formatDuration(gint64 us) {
    gint64 s = us / G_USEC_PER_SEC;
    char buf[32];
    snprintf(buf, sizeof buf, "%02lld:%02lld:%02lld", (long long)(s / 3600), (long long)((s / 60) % 60), (long long)(s % 60));
    return buf;
}
} // namespace

/* ------------------------------------------------------------ costruzione */

AppWindow::AppWindow(GtkApplication* app) : app_(app) {
    buildUi();

    pipeline_.onError = [this](const std::string& e) {
        showMessage("Errore: " + e, GTK_MESSAGE_ERROR);
        if (pipeline_.isRecording()) toggleRecording();
        // Periferica scollegata o flusso interrotto: ferma la pipeline e torna al segnaposto.
        if (e.find("Internal data stream") != std::string::npos || e.find("Could not read") != std::string::npos ||
            e.find("Device") != std::string::npos || e.find("dispositivo") != std::string::npos) {
            if (!pipeline_.isRecording()) stopSource();
        }
    };
    pipeline_.onInfo = [this](const std::string& m) { setStatus(m); };
    pipeline_.onAudioLevel = [this](double db) {
        double v = std::clamp((db + 60.0) / 60.0, 0.0, 1.0);
        gtk_level_bar_set_value(GTK_LEVEL_BAR(levelBar_), v);
    };
    pipeline_.onVideoInfo = [this](int w, int h, double fps) {
        char buf[64];
        snprintf(buf, sizeof buf, "%d×%d  %.4g fps", w, h, fps);
        lastVideoInfo_ = buf;
        const VideoSource* s = currentSource();
        std::string name = s ? (s->kind == SourceKind::Webcam ? s->name : s->kind == SourceKind::Dvb ? s->dvb.name : s->label()) : "";
        gtk_label_set_text(GTK_LABEL(videoInfoLabel_), (name.empty() ? lastVideoInfo_ : name + "  ·  " + lastVideoInfo_).c_str());
        gtk_widget_show(videoInfoLabel_);
        updateSubtitle();
    };
    pipeline_.onExternalWindowClosed = [this] {
        suppressSignals_ = true;
        gtk_switch_set_active(GTK_SWITCH(extWinSwitch_), FALSE);
        suppressSignals_ = false;
    };
    devices_.onDevicesChanged = [this] { onDevicesChanged(); };

    populateSources();
    populateAudioSources();
    populateOutputs();

    // Ripristino impostazioni
    suppressSignals_ = true;
    std::string savedSource = settings_.getString("source", "test");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(sourceCombo_), savedSource.c_str());
    bool savedMissing = gtk_combo_box_get_active(GTK_COMBO_BOX(sourceCombo_)) < 0;
    if (savedMissing) gtk_combo_box_set_active(GTK_COMBO_BOX(sourceCombo_), 0);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(flipCombo_), settings_.getString("flip", "none").c_str());
    gtk_entry_set_text(GTK_ENTRY(uriEntry_), settings_.getString("uri", "").c_str());
    suppressSignals_ = false;
    // Se la periferica usata l'ultima volta non è collegata, non avviare nulla: mostra il segnaposto
    // con il tasto "Cerca dispositivi" invece di passare in silenzio alla sorgente di prova.
    onSourceChanged(!savedMissing);
    channelsFile_ = settings_.getString("channels_file", dvb::defaultChannelsConfPath());
    populateChannels();
    updateDeviceStatus();
    if (savedMissing && savedSource != "test" && savedSource.rfind("/dev/", 0) == 0) {
        std::string lastName = settings_.getString("source_name", savedSource);
        gtk_label_set_text(GTK_LABEL(placeholderTitle_), ("Periferica non rilevata: " + lastName).c_str());
        setStatus("La periferica usata l'ultima volta (" + lastName + ") non è collegata. Collegala e premi Aggiorna.");
    } else {
        setStatus("Pronto. Seleziona una sorgente e premi Avvia.");
    }
}

AppWindow::~AppWindow() {
    if (recordTimer_) g_source_remove(recordTimer_);
}

void AppWindow::addAction(const char* name, void (AppWindow::*fn)()) {
    GSimpleAction* a = g_simple_action_new(name, nullptr);
    struct Closure { AppWindow* self; void (AppWindow::*fn)(); };
    auto* c = new Closure { this, fn };
    g_signal_connect_data(a, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer d) {
        auto* cl = static_cast<Closure*>(d);
        (cl->self->*(cl->fn))();
    }), c, [](gpointer d, GClosure*) { delete static_cast<Closure*>(d); }, GConnectFlags(0));
    g_action_map_add_action(G_ACTION_MAP(window_), G_ACTION(a));
    g_object_unref(a);
}

GtkWidget* AppWindow::row(GtkWidget* box, const char* label, GtkWidget* w) {
    GtkWidget* r = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    if (label) {
        GtkWidget* l = gtk_label_new(label);
        gtk_label_set_xalign(GTK_LABEL(l), 0);
        gtk_widget_set_size_request(l, 92, -1);
        gtk_box_pack_start(GTK_BOX(r), l, FALSE, FALSE, 0);
    }
    gtk_widget_set_hexpand(w, TRUE);
    gtk_box_pack_start(GTK_BOX(r), w, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), r, FALSE, FALSE, 0);
    return r;
}

GtkWidget* AppWindow::heading(GtkWidget* box, const char* text) {
    GtkWidget* l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(l), "heading");
    gtk_box_pack_start(GTK_BOX(box), l, FALSE, FALSE, 0);
    return l;
}

GtkWidget* AppWindow::compactCombo() {
    GtkWidget* c = gtk_combo_box_text_new();
    GList* cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(c));
    for (GList* l = cells; l; l = l->next) g_object_set(l->data, "ellipsize", PANGO_ELLIPSIZE_END, "width-chars", 8, nullptr);
    g_list_free(cells);
    return c;
}

GtkWidget* AppWindow::note(GtkWidget* box, const char* text) {
    GtkWidget* l = gtk_label_new(text);
    gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 34);
    gtk_style_context_add_class(gtk_widget_get_style_context(l), "dim");
    gtk_box_pack_start(GTK_BOX(box), l, FALSE, FALSE, 0);
    return l;
}

void AppWindow::buildUi() {
    window_ = gtk_application_window_new(app_);
    gtk_window_set_title(GTK_WINDOW(window_), APP_TITLE);
    gtk_window_set_default_size(GTK_WINDOW(window_), 800, 600);
    gtk_window_set_icon_name(GTK_WINDOW(window_), APP_ID);
    g_signal_connect(window_, "key-press-event", G_CALLBACK(keyPressCb), this);
    g_signal_connect(window_, "delete-event", G_CALLBACK(deleteCb), this);

    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, CSS, -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    // Azioni finestra (menu + scorciatoie)
    addAction("start", &AppWindow::startSource);
    addAction("record", &AppWindow::toggleRecording);
    addAction("snapshot", &AppWindow::takeSnapshot);
    addAction("refresh", &AppWindow::onDevicesChanged);
    addAction("open-folder", &AppWindow::openRecordFolder);
    addAction("fullscreen", &AppWindow::toggleFullscreen);
    addAction("about", &AppWindow::showAbout);
    const char* recAccels[] = { "<Control>r", nullptr };
    const char* snapAccels[] = { "<Control>s", nullptr };
    const char* fsAccels[] = { "F11", nullptr };
    gtk_application_set_accels_for_action(app_, "win.record", recAccels);
    gtk_application_set_accels_for_action(app_, "win.snapshot", snapAccels);
    gtk_application_set_accels_for_action(app_, "win.fullscreen", fsAccels);

    // Header bar in stile GNOME
    headerBar_ = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerBar_), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(headerBar_), APP_TITLE);
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(headerBar_), "Nessuna sorgente");
    gtk_window_set_titlebar(GTK_WINDOW(window_), headerBar_);

    startButton_ = gtk_button_new_from_icon_name("media-playback-start-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(startButton_, "Avvia / ferma la sorgente");
    g_signal_connect(startButton_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer self) {
        if (SELF->pipeline_.isRunning()) SELF->stopSource(); else SELF->startSource();
    }), this);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(headerBar_), startButton_);

    snapshotButton_ = gtk_button_new_from_icon_name("camera-photo-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(snapshotButton_, "Scatta un'istantanea PNG (Ctrl+S)");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(snapshotButton_), "win.snapshot");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(headerBar_), snapshotButton_);

    GtkWidget* menuButton = gtk_menu_button_new();
    gtk_button_set_image(GTK_BUTTON(menuButton), gtk_image_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_BUTTON));
    GMenu* menu = g_menu_new();
    g_menu_append(menu, "Aggiorna dispositivi", "win.refresh");
    g_menu_append(menu, "Apri cartella registrazioni", "win.open-folder");
    g_menu_append(menu, "Schermo intero", "win.fullscreen");
    g_menu_append(menu, "Informazioni", "win.about");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menuButton), G_MENU_MODEL(menu));
    g_object_unref(menu);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(headerBar_), menuButton);

    recordButton_ = gtk_button_new_from_icon_name("media-record-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(recordButton_, "Avvia / ferma la registrazione (Ctrl+R)");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(recordButton_), "win.record");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(headerBar_), recordButton_);

    // Corpo
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window_), vbox);

    infoBar_ = gtk_info_bar_new();
    gtk_info_bar_set_show_close_button(GTK_INFO_BAR(infoBar_), TRUE);
    gtk_info_bar_set_revealed(GTK_INFO_BAR(infoBar_), FALSE);
    infoLabel_ = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(infoLabel_), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(infoLabel_), 80);
    gtk_label_set_xalign(GTK_LABEL(infoLabel_), 0);
    gtk_container_add(GTK_CONTAINER(gtk_info_bar_get_content_area(GTK_INFO_BAR(infoBar_))), infoLabel_);
    g_signal_connect(infoBar_, "response", G_CALLBACK(+[](GtkInfoBar* b, gint, gpointer) { gtk_info_bar_set_revealed(b, FALSE); }), nullptr);
    gtk_box_pack_start(GTK_BOX(vbox), infoBar_, FALSE, FALSE, 0);

    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);

    // Area video con overlay
    GtkWidget* overlay = gtk_overlay_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(overlay), "video-area");
    gtk_widget_set_hexpand(overlay, TRUE);
    gtk_widget_set_vexpand(overlay, TRUE);
    GtkWidget* video = pipeline_.videoWidget();
    gtk_widget_set_size_request(video, 320, 240);
    gtk_container_add(GTK_CONTAINER(overlay), video);

    recBadge_ = gtk_label_new("● REC 00:00:00");
    gtk_style_context_add_class(gtk_widget_get_style_context(recBadge_), "rec-badge");
    gtk_widget_set_halign(recBadge_, GTK_ALIGN_START);
    gtk_widget_set_valign(recBadge_, GTK_ALIGN_START);
    gtk_widget_set_no_show_all(recBadge_, TRUE);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), recBadge_);

    videoInfoLabel_ = gtk_label_new("");
    gtk_style_context_add_class(gtk_widget_get_style_context(videoInfoLabel_), "video-info");
    gtk_widget_set_halign(videoInfoLabel_, GTK_ALIGN_END);
    gtk_widget_set_valign(videoInfoLabel_, GTK_ALIGN_END);
    gtk_widget_set_no_show_all(videoInfoLabel_, TRUE);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), videoInfoLabel_);

    // Segnaposto mostrato quando nessuna sorgente è attiva: nome delle periferiche rilevate e tasto di ricerca.
    placeholder_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(placeholder_), "placeholder-bg");
    gtk_widget_set_halign(placeholder_, GTK_ALIGN_FILL);
    gtk_widget_set_valign(placeholder_, GTK_ALIGN_FILL);
    GtkWidget* phInner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(phInner), "placeholder");
    gtk_widget_set_halign(phInner, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(phInner, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(placeholder_), phInner, TRUE, FALSE, 0);
    GtkWidget* phIcon = gtk_image_new_from_icon_name("camera-web-symbolic", GTK_ICON_SIZE_DIALOG);
    gtk_image_set_pixel_size(GTK_IMAGE(phIcon), 64);
    gtk_box_pack_start(GTK_BOX(phInner), phIcon, FALSE, FALSE, 0);
    placeholderTitle_ = gtk_label_new("");
    gtk_style_context_add_class(gtk_widget_get_style_context(placeholderTitle_), "placeholder-title");
    gtk_label_set_justify(GTK_LABEL(placeholderTitle_), GTK_JUSTIFY_CENTER);
    gtk_label_set_line_wrap(GTK_LABEL(placeholderTitle_), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(placeholderTitle_), 34);
    gtk_box_pack_start(GTK_BOX(phInner), placeholderTitle_, FALSE, FALSE, 0);
    placeholderText_ = gtk_label_new("");
    gtk_label_set_justify(GTK_LABEL(placeholderText_), GTK_JUSTIFY_CENTER);
    gtk_label_set_line_wrap(GTK_LABEL(placeholderText_), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(placeholderText_), 38);
    gtk_box_pack_start(GTK_BOX(phInner), placeholderText_, FALSE, FALSE, 0);
    GtkWidget* phBtns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(phBtns, GTK_ALIGN_CENTER);
    GtkWidget* phRefresh = gtk_button_new_with_label("Cerca dispositivi");
    gtk_button_set_image(GTK_BUTTON(phRefresh), gtk_image_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(phRefresh), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(phRefresh), "suggested-action");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(phRefresh), "win.refresh");
    GtkWidget* phStart = gtk_button_new_with_label("Avvia sorgente");
    gtk_button_set_image(GTK_BUTTON(phStart), gtk_image_new_from_icon_name("media-playback-start-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(phStart), TRUE);
    gtk_actionable_set_action_name(GTK_ACTIONABLE(phStart), "win.start");
    gtk_box_pack_start(GTK_BOX(phBtns), phRefresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(phBtns), phStart, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(phInner), phBtns, FALSE, FALSE, 4);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), placeholder_);
    gtk_widget_show_all(placeholder_);                 // i figli restano "mostrati": basta show/hide sul contenitore
    gtk_widget_hide(placeholder_);
    gtk_widget_set_no_show_all(placeholder_, TRUE);    // show_all della finestra non deve riaprirlo
    gtk_box_pack_start(GTK_BOX(hbox), overlay, TRUE, TRUE, 0);

    // Barra laterale con le pagine
    sidebar_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_size_request(sidebar_, 300, -1);
    gtk_widget_set_hexpand(sidebar_, FALSE);
    stack_ = gtk_stack_new();
    gtk_stack_set_hhomogeneous(GTK_STACK(stack_), TRUE);
    gtk_stack_set_transition_type(GTK_STACK(stack_), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    GtkWidget* switcher = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(switcher), "linked");
    gtk_widget_set_halign(switcher, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(switcher, 6);
    gtk_box_pack_start(GTK_BOX(sidebar_), switcher, FALSE, FALSE, 0);
    struct Page { const char* name; const char* title; const char* icon; };
    GtkWidget* group = nullptr;
    for (Page pg : { Page{ "source", "Sorgente", "camera-video-symbolic" }, Page{ "webcam", "Webcam", "camera-web-symbolic" },
                     Page{ "tv", "TV e satellite", "tv-symbolic" }, Page{ "record", "Registrazione", "media-record-symbolic" },
                     Page{ "output", "Uscite (ponte segnale)", "video-display-symbolic" } }) {
        GtkWidget* b = gtk_radio_button_new_from_widget(GTK_RADIO_BUTTON(group));
        if (!group) group = b;
        gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(b), FALSE);
        GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_box_pack_start(GTK_BOX(content), gtk_image_new_from_icon_name(pg.icon, GTK_ICON_SIZE_LARGE_TOOLBAR), FALSE, FALSE, 0);
        GtkWidget* lbl = gtk_label_new(pg.name == std::string("source") ? "Sorgente" : pg.name == std::string("webcam") ? "Webcam" :
                                       pg.name == std::string("tv") ? "TV" : pg.name == std::string("record") ? "Rec" : "Uscite");
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "tab-label");
        gtk_box_pack_start(GTK_BOX(content), lbl, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(b), content);
        gtk_widget_set_tooltip_text(b, pg.title);
        g_object_set_data(G_OBJECT(b), "page", const_cast<char*>(pg.name));
        g_signal_connect(b, "toggled", G_CALLBACK(+[](GtkToggleButton* t, gpointer self) {
            if (gtk_toggle_button_get_active(t))
                gtk_stack_set_visible_child_name(GTK_STACK(SELF->stack_), static_cast<const char*>(g_object_get_data(G_OBJECT(t), "page")));
        }), this);
        gtk_box_pack_start(GTK_BOX(switcher), b, FALSE, FALSE, 0);
    }
    // Mantiene i pulsanti allineati quando la pagina viene cambiata da codice.
    g_signal_connect(stack_, "notify::visible-child-name", G_CALLBACK(+[](GObject* st, GParamSpec*, gpointer sw) {
        const gchar* name = gtk_stack_get_visible_child_name(GTK_STACK(st));
        GList* kids = gtk_container_get_children(GTK_CONTAINER(sw));
        for (GList* l = kids; l && name; l = l->next)
            if (g_strcmp0(static_cast<const char*>(g_object_get_data(G_OBJECT(l->data), "page")), name) == 0 &&
                !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(l->data)))
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(l->data), TRUE);
        g_list_free(kids);
    }), switcher);
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), stack_);
    gtk_box_pack_start(GTK_BOX(sidebar_), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), sidebar_, FALSE, FALSE, 0);

    gtk_stack_add_titled(GTK_STACK(stack_), buildSourcePage(), "source", "Sorgente");
    gtk_stack_add_titled(GTK_STACK(stack_), buildWebcamPage(), "webcam", "Webcam");
    gtk_stack_add_titled(GTK_STACK(stack_), buildTvPage(), "tv", "TV");
    gtk_stack_add_titled(GTK_STACK(stack_), buildRecordPage(), "record", "Registra");
    gtk_stack_add_titled(GTK_STACK(stack_), buildOutputPage(), "output", "Uscite");

    // Barra di stato
    bottomBar_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(bottomBar_), "status");
    statusLabel_ = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(statusLabel_), 0);
    gtk_label_set_ellipsize(GTK_LABEL(statusLabel_), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(bottomBar_), statusLabel_, TRUE, TRUE, 0);
    GtkWidget* micIcon = gtk_image_new_from_icon_name("audio-input-microphone-symbolic", GTK_ICON_SIZE_MENU);
    gtk_box_pack_start(GTK_BOX(bottomBar_), micIcon, FALSE, FALSE, 0);
    levelBar_ = gtk_level_bar_new_for_interval(0, 1);
    gtk_widget_set_size_request(levelBar_, 120, -1);
    gtk_widget_set_valign(levelBar_, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(bottomBar_), levelBar_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), bottomBar_, FALSE, FALSE, 0);

    gtk_widget_show_all(window_);
}

GtkWidget* AppWindow::buildSourcePage() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "page");

    // Barra "periferica di cattura": nome della webcam rilevata (o avviso) + tasto Aggiorna
    GtkWidget* devBar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(devBar), "device-bar");
    GtkWidget* devIcon = gtk_image_new_from_icon_name("camera-web-symbolic", GTK_ICON_SIZE_LARGE_TOOLBAR);
    gtk_widget_set_valign(devIcon, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(devBar), devIcon, FALSE, FALSE, 0);
    deviceLabel_ = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(deviceLabel_), 0);
    gtk_label_set_line_wrap(GTK_LABEL(deviceLabel_), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(deviceLabel_), 22);
    gtk_widget_set_hexpand(deviceLabel_, TRUE);
    gtk_box_pack_start(GTK_BOX(devBar), deviceLabel_, TRUE, TRUE, 0);
    GtkWidget* devRefresh = gtk_button_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(devRefresh, "Cerca di nuovo le periferiche di cattura (webcam, schede TV)");
    gtk_widget_set_valign(devRefresh, GTK_ALIGN_CENTER);
    gtk_actionable_set_action_name(GTK_ACTIONABLE(devRefresh), "win.refresh");
    gtk_box_pack_start(GTK_BOX(devBar), devRefresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), devBar, FALSE, FALSE, 0);

    heading(box, "Sorgente video");
    sourceCombo_ = compactCombo();
    g_signal_connect(sourceCombo_, "changed", G_CALLBACK(+[](GtkComboBox*, gpointer self) {
        if (!SELF->suppressSignals_) SELF->onSourceChanged();
    }), this);
    gtk_box_pack_start(GTK_BOX(box), sourceCombo_, FALSE, FALSE, 0);

    formatCombo_ = compactCombo();
    g_signal_connect(formatCombo_, "changed", G_CALLBACK(+[](GtkComboBox*, gpointer self) {
        if (!SELF->suppressSignals_) SELF->restartIfRunning();
    }), this);
    formatRow_ = row(box, "Formato", formatCombo_);

    uriEntry_ = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(uriEntry_), "rtsp://…  http://…  file:///…");
    g_signal_connect(uriEntry_, "activate", G_CALLBACK(+[](GtkEntry*, gpointer self) { SELF->startSource(); }), this);
    uriRow_ = row(box, "URL", uriEntry_);

    heading(box, "Audio");
    audioCombo_ = compactCombo();
    g_signal_connect(audioCombo_, "changed", G_CALLBACK(+[](GtkComboBox*, gpointer self) {
        if (!SELF->suppressSignals_) SELF->restartIfRunning();
    }), this);
    row(box, "Ingresso", audioCombo_);
    micScale_ = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 2.0, 0.05);
    gtk_range_set_value(GTK_RANGE(micScale_), 1.0);
    gtk_scale_add_mark(GTK_SCALE(micScale_), 1.0, GTK_POS_BOTTOM, nullptr);
    gtk_scale_set_draw_value(GTK_SCALE(micScale_), FALSE);
    g_signal_connect(micScale_, "value-changed", G_CALLBACK(+[](GtkRange* r, gpointer self) {
        SELF->pipeline_.setMicVolume(gtk_range_get_value(r));
    }), this);
    row(box, "Guadagno", micScale_);

    heading(box, "Immagine");
    flipCombo_ = compactCombo();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(flipCombo_), "none", "Normale");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(flipCombo_), "horizontal-flip", "Specchiata (orizzontale)");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(flipCombo_), "vertical-flip", "Capovolta (verticale)");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(flipCombo_), "rotate-180", "Ruotata 180°");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(flipCombo_), "clockwise", "Ruotata 90° orario");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(flipCombo_), "counterclockwise", "Ruotata 90° antiorario");
    gtk_combo_box_set_active(GTK_COMBO_BOX(flipCombo_), 0);
    g_signal_connect(flipCombo_, "changed", G_CALLBACK(+[](GtkComboBox* c, gpointer self) {
        const gchar* id = gtk_combo_box_get_active_id(c);
        if (id) SELF->pipeline_.setFlip(id);
    }), this);
    row(box, "Orientamento", flipCombo_);

    GtkWidget* btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_top(btns, 8);
    GtkWidget* start = gtk_button_new_with_label("Avvia");
    gtk_style_context_add_class(gtk_widget_get_style_context(start), "suggested-action");
    g_signal_connect(start, "clicked", G_CALLBACK(+[](GtkButton*, gpointer self) { SELF->startSource(); }), this);
    GtkWidget* stop = gtk_button_new_with_label("Ferma");
    g_signal_connect(stop, "clicked", G_CALLBACK(+[](GtkButton*, gpointer self) { SELF->stopSource(); }), this);
    GtkWidget* refresh = gtk_button_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(refresh, "Aggiorna l'elenco dei dispositivi");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(refresh), "win.refresh");
    gtk_box_pack_start(GTK_BOX(btns), start, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btns), stop, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btns), refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), btns, FALSE, FALSE, 0);

    GtkWidget* hint = note(box, "Le webcam UVC (Logitech C920, Brio…) sono gestite dal driver del kernel uvcvideo; "
                                    "il microfono USB da snd-usb-audio. Le schede TV compaiono come adattatori DVB.");
    gtk_widget_set_margin_top(hint, 10);
    return box;
}

GtkWidget* AppWindow::buildWebcamPage() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "page");
    webcamInfo_ = gtk_label_new("Nessuna webcam selezionata.");
    gtk_label_set_line_wrap(GTK_LABEL(webcamInfo_), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(webcamInfo_), 34);
    gtk_label_set_xalign(GTK_LABEL(webcamInfo_), 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(webcamInfo_), "dim");
    gtk_box_pack_start(GTK_BOX(box), webcamInfo_, FALSE, FALSE, 0);

    heading(box, "Profili rapidi");
    GtkWidget* presets = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_set_homogeneous(GTK_BOX(presets), FALSE);
    struct P { const char* label; const char* tip; int id; };
    for (P p : { P{ "Auto", "Ripristina i valori predefiniti del driver", 0 },
                 P{ "50 Hz", "Anti-sfarfallio per luci a 50 Hz (Italia), esposizione automatica", 1 },
                 P{ "Fuoco fisso", "Disattiva l'autofocus continuo (utile per documenti e oggetti vicini)", 2 } }) {
        GtkWidget* b = gtk_button_new_with_label(p.label);
        gtk_widget_set_tooltip_text(b, p.tip);
        g_object_set_data(G_OBJECT(b), "preset", GINT_TO_POINTER(p.id));
        g_signal_connect(b, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer self) {
            SELF->applyPreset(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "preset")));
        }), this);
        gtk_box_pack_start(GTK_BOX(presets), b, TRUE, TRUE, 0);
    }
    gtk_box_pack_start(GTK_BOX(box), presets, FALSE, FALSE, 0);

    heading(box, "Controlli del dispositivo (V4L2)");
    webcamControlsBox_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(box), webcamControlsBox_, FALSE, FALSE, 0);
    return box;
}

GtkWidget* AppWindow::buildTvPage() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "page");
    tvInfo_ = gtk_label_new("Nessuna scheda TV (DVB) selezionata.");
    gtk_label_set_line_wrap(GTK_LABEL(tvInfo_), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(tvInfo_), 34);
    gtk_label_set_xalign(GTK_LABEL(tvInfo_), 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(tvInfo_), "dim");
    gtk_box_pack_start(GTK_BOX(box), tvInfo_, FALSE, FALSE, 0);

    heading(box, "Canali");
    channelCombo_ = compactCombo();
    g_signal_connect(channelCombo_, "changed", G_CALLBACK(+[](GtkComboBox*, gpointer self) {
        if (SELF->suppressSignals_) return;
        SELF->manualTune_ = false;
        const VideoSource* s = SELF->currentSource();
        if (s && s->kind == SourceKind::Dvb) SELF->startSource();
    }), this);
    gtk_box_pack_start(GTK_BOX(box), channelCombo_, FALSE, FALSE, 0);

    channelsFileLabel_ = gtk_label_new("");
    gtk_label_set_ellipsize(GTK_LABEL(channelsFileLabel_), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_xalign(GTK_LABEL(channelsFileLabel_), 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(channelsFileLabel_), "dim");
    gtk_box_pack_start(GTK_BOX(box), channelsFileLabel_, FALSE, FALSE, 0);

    GtkWidget* chBtns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget* load = gtk_button_new_with_label("Carica file…");
    g_signal_connect(load, "clicked", G_CALLBACK(+[](GtkButton*, gpointer self) { SELF->chooseChannelsFile(); }), this);
    GtkWidget* scan = gtk_button_new_with_label("Scansione…");
    gtk_widget_set_tooltip_text(scan, "Esegue dvbv5-scan (pacchetto dvb-tools) con un file di scansione iniziale");
    g_signal_connect(scan, "clicked", G_CALLBACK(+[](GtkButton*, gpointer self) { SELF->runChannelScan(); }), this);
    gtk_box_pack_start(GTK_BOX(chBtns), load, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(chBtns), scan, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), chBtns, FALSE, FALSE, 0);

    GtkWidget* logExp = gtk_expander_new("Registro scansione");
    scanLog_ = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(scanLog_), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(scanLog_), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(scanLog_), GTK_WRAP_WORD_CHAR);
    GtkWidget* logScroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_widget_set_size_request(logScroll, -1, 120);
    gtk_container_add(GTK_CONTAINER(logScroll), scanLog_);
    gtk_container_add(GTK_CONTAINER(logExp), logScroll);
    gtk_box_pack_start(GTK_BOX(box), logExp, FALSE, FALSE, 0);

    heading(box, "Sintonia manuale");
    delsysCombo_ = compactCombo();
    for (const auto& d : dvb::deliverySystems()) gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(delsysCombo_), d.first.c_str(), d.second.c_str());
    gtk_combo_box_set_active(GTK_COMBO_BOX(delsysCombo_), 0);
    row(box, "Sistema", delsysCombo_);
    freqSpin_ = gtk_spin_button_new_with_range(1, 15000, 0.125);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(freqSpin_), 3);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(freqSpin_), 634.0);   // canale UHF 40 (Italia)
    row(box, "Frequenza MHz", freqSpin_);
    bandwidthCombo_ = compactCombo();
    for (const char* b : { "8", "7", "6", "5", "10", "1.712" }) gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(bandwidthCombo_), b, (std::string(b) + " MHz").c_str());
    gtk_combo_box_set_active(GTK_COMBO_BOX(bandwidthCombo_), 0);
    row(box, "Banda", bandwidthCombo_);
    symbolSpin_ = gtk_spin_button_new_with_range(1000, 45000, 500);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(symbolSpin_), 27500);
    row(box, "Symbol rate", symbolSpin_);
    polarityCombo_ = compactCombo();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(polarityCombo_), "h", "Orizzontale");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(polarityCombo_), "v", "Verticale");
    gtk_combo_box_set_active(GTK_COMBO_BOX(polarityCombo_), 0);
    row(box, "Polarità (SAT)", polarityCombo_);
    GtkWidget* tune = gtk_button_new_with_label("Sintonizza");
    g_signal_connect(tune, "clicked", G_CALLBACK(+[](GtkButton*, gpointer self) {
        SELF->manualTune_ = true;
        SELF->startSource();
    }), this);
    gtk_box_pack_start(GTK_BOX(box), tune, FALSE, FALSE, 0);
    return box;
}

GtkWidget* AppWindow::buildRecordPage() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "page");

    heading(box, "Formato");
    fmtMp4_ = gtk_radio_button_new_with_label(nullptr, "MP4 (H.264 + AAC)");
    fmtOgg_ = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(fmtMp4_), "OGG (Theora + Vorbis)");
    gtk_box_pack_start(GTK_BOX(box), fmtMp4_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), fmtOgg_, FALSE, FALSE, 0);
    encoderInfo_ = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(encoderInfo_), 0);
    gtk_label_set_line_wrap(GTK_LABEL(encoderInfo_), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(encoderInfo_), 34);
    gtk_style_context_add_class(gtk_widget_get_style_context(encoderInfo_), "dim");
    gtk_box_pack_start(GTK_BOX(box), encoderInfo_, FALSE, FALSE, 0);
    auto updEnc = +[](GtkToggleButton*, gpointer self) {
        bool mp4 = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(SELF->fmtMp4_));
        gtk_label_set_text(GTK_LABEL(SELF->encoderInfo_),
                           ("Encoder: " + MediaPipeline::describeEncoders(mp4 ? MediaPipeline::RecordFormat::Mp4 : MediaPipeline::RecordFormat::Ogg)).c_str());
    };
    g_signal_connect(fmtMp4_, "toggled", G_CALLBACK(updEnc), this);
    if (settings_.getString("record_format", "mp4") == "ogg") gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fmtOgg_), TRUE);
    updEnc(nullptr, this);

    heading(box, "Qualità");
    videoKbps_ = gtk_spin_button_new_with_range(300, 30000, 100);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(videoKbps_), settings_.getInt("video_kbps", 4000));
    row(box, "Video kbit/s", videoKbps_);
    audioKbps_ = gtk_spin_button_new_with_range(32, 320, 16);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(audioKbps_), settings_.getInt("audio_kbps", 128));
    row(box, "Audio kbit/s", audioKbps_);
    recAudioCheck_ = gtk_check_button_new_with_label("Registra anche l'audio");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(recAudioCheck_), settings_.getBool("record_audio", true));
    gtk_box_pack_start(GTK_BOX(box), recAudioCheck_, FALSE, FALSE, 0);

    heading(box, "Destinazione");
    folderButton_ = gtk_file_chooser_button_new("Cartella di destinazione", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
    std::string defFolder = g_get_user_special_dir(G_USER_DIRECTORY_VIDEOS) ? g_get_user_special_dir(G_USER_DIRECTORY_VIDEOS) : g_get_home_dir();
    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(folderButton_), settings_.getString("record_folder", defFolder).c_str());
    row(box, "Cartella", folderButton_);
    prefixEntry_ = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(prefixEntry_), settings_.getString("prefix", "SmartVision").c_str());
    row(box, "Prefisso", prefixEntry_);

    bigRecordButton_ = gtk_button_new_with_label("●  Avvia registrazione");
    gtk_style_context_add_class(gtk_widget_get_style_context(bigRecordButton_), "destructive-action");
    gtk_widget_set_margin_top(bigRecordButton_, 8);
    gtk_actionable_set_action_name(GTK_ACTIONABLE(bigRecordButton_), "win.record");
    gtk_box_pack_start(GTK_BOX(box), bigRecordButton_, FALSE, FALSE, 0);

    lastFileLabel_ = gtk_label_new("");
    gtk_label_set_ellipsize(GTK_LABEL(lastFileLabel_), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_xalign(GTK_LABEL(lastFileLabel_), 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(lastFileLabel_), "dim");
    gtk_box_pack_start(GTK_BOX(box), lastFileLabel_, FALSE, FALSE, 0);
    GtkWidget* open = gtk_button_new_with_label("Apri cartella");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(open), "win.open-folder");
    gtk_box_pack_start(GTK_BOX(box), open, FALSE, FALSE, 0);
    return box;
}

GtkWidget* AppWindow::buildOutputPage() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "page");

    heading(box, "Ponte video → dispositivo V4L2");
    note(box, "Invia il segnale passante a un dispositivo di uscita video (v4l2loopback o "
                                  "uscita hardware). Altre applicazioni lo vedranno come una webcam.");
    videoOutCombo_ = compactCombo();
    row(box, "Dispositivo", videoOutCombo_);
    videoOutSwitch_ = gtk_switch_new();
    gtk_widget_set_halign(videoOutSwitch_, GTK_ALIGN_START);
    g_signal_connect(videoOutSwitch_, "notify::active", G_CALLBACK(+[](GObject*, GParamSpec*, gpointer self) {
        if (!SELF->suppressSignals_) SELF->onVideoOutputToggled();
    }), this);
    row(box, "Attivo", videoOutSwitch_);
    GtkWidget* mk = gtk_button_new_with_label("Crea dispositivo virtuale…");
    gtk_widget_set_tooltip_text(mk, "Carica il modulo v4l2loopback (richiede la password di amministratore)");
    g_signal_connect(mk, "clicked", G_CALLBACK(+[](GtkButton*, gpointer self) { SELF->createLoopbackDevice(); }), this);
    gtk_box_pack_start(GTK_BOX(box), mk, FALSE, FALSE, 0);

    heading(box, "Ponte audio → uscita hardware");
    audioOutCombo_ = compactCombo();
    row(box, "Dispositivo", audioOutCombo_);
    audioOutSwitch_ = gtk_switch_new();
    gtk_widget_set_halign(audioOutSwitch_, GTK_ALIGN_START);
    g_signal_connect(audioOutSwitch_, "notify::active", G_CALLBACK(+[](GObject*, GParamSpec*, gpointer self) {
        if (!SELF->suppressSignals_) SELF->onAudioOutputToggled();
    }), this);
    row(box, "Attivo", audioOutSwitch_);
    outVolume_ = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1.5, 0.05);
    gtk_range_set_value(GTK_RANGE(outVolume_), 1.0);
    gtk_scale_set_draw_value(GTK_SCALE(outVolume_), FALSE);
    gtk_scale_add_mark(GTK_SCALE(outVolume_), 1.0, GTK_POS_BOTTOM, nullptr);
    g_signal_connect(outVolume_, "value-changed", G_CALLBACK(+[](GtkRange* r, gpointer self) {
        SELF->pipeline_.setOutputVolume(gtk_range_get_value(r));
    }), this);
    row(box, "Volume", outVolume_);
    outMute_ = gtk_check_button_new_with_label("Muto");
    g_signal_connect(outMute_, "toggled", G_CALLBACK(+[](GtkToggleButton* t, gpointer self) {
        SELF->pipeline_.setOutputMuted(gtk_toggle_button_get_active(t));
    }), this);
    gtk_box_pack_start(GTK_BOX(box), outMute_, FALSE, FALSE, 0);

    heading(box, "Finestra esterna (secondo schermo / TV HDMI)");
    extWinSwitch_ = gtk_switch_new();
    gtk_widget_set_halign(extWinSwitch_, GTK_ALIGN_START);
    g_signal_connect(extWinSwitch_, "notify::active", G_CALLBACK(+[](GObject*, GParamSpec*, gpointer self) {
        if (!SELF->suppressSignals_) SELF->onExternalWindowToggled();
    }), this);
    row(box, "Attiva", extWinSwitch_);
    note(box, "Apre una seconda finestra con lo stesso segnale: trascinala sul monitor/TV e premi F11.");
    return box;
}

/* ------------------------------------------------------------- popolamento */

void AppWindow::populateSources() {
    suppressSignals_ = true;
    const gchar* prev = gtk_combo_box_get_active_id(GTK_COMBO_BOX(sourceCombo_));
    std::string prevId = prev ? prev : "";
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(sourceCombo_));
    for (const auto& s : devices_.videoSources())
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sourceCombo_), s.id.c_str(), s.label().c_str());
    if (!prevId.empty()) gtk_combo_box_set_active_id(GTK_COMBO_BOX(sourceCombo_), prevId.c_str());
    if (gtk_combo_box_get_active(GTK_COMBO_BOX(sourceCombo_)) < 0) gtk_combo_box_set_active(GTK_COMBO_BOX(sourceCombo_), 0);
    suppressSignals_ = false;
}

void AppWindow::populateFormats() {
    suppressSignals_ = true;
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(formatCombo_));
    const VideoSource* s = currentSource();
    if (s && s->kind == SourceKind::Webcam) {
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(formatCombo_), "auto", "Automatico");
        std::string best;
        int bestScore = -1;
        for (const auto& f : s->formats) {
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(formatCombo_), f.key().c_str(), f.label().c_str());
            // Preferenza: MJPEG 1280x720 a 30 fps (basso carico USB, ottima qualità), poi 1080p.
            double fps = f.fpsDen ? double(f.fpsNum) / f.fpsDen : 0;
            int score = 0;
            if (f.fourcc == "MJPG") score += 100;
            if (f.width == 1280 && f.height == 720) score += 50;
            else if (f.width == 1920 && f.height == 1080) score += 40;
            if (fps >= 29 && fps <= 31) score += 20;
            if (score > bestScore) { bestScore = score; best = f.key(); }
        }
        std::string saved = settings_.getString(("format:" + s->id).c_str(), best);
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(formatCombo_), saved.c_str());
        if (gtk_combo_box_get_active(GTK_COMBO_BOX(formatCombo_)) < 0) gtk_combo_box_set_active_id(GTK_COMBO_BOX(formatCombo_), best.c_str());
        if (gtk_combo_box_get_active(GTK_COMBO_BOX(formatCombo_)) < 0) gtk_combo_box_set_active(GTK_COMBO_BOX(formatCombo_), 0);
    }
    suppressSignals_ = false;
}

void AppWindow::populateAudioSources() {
    suppressSignals_ = true;
    const gchar* prev = gtk_combo_box_get_active_id(GTK_COMBO_BOX(audioCombo_));
    std::string prevId = prev ? prev : settings_.getString("audio", "auto");
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(audioCombo_));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(audioCombo_), "auto", "Automatico (abbinato alla sorgente)");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(audioCombo_), "source", "Audio della sorgente (TV / URL)");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(audioCombo_), "none", "Nessun audio");
    for (const auto& a : devices_.audioSources())
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(audioCombo_), ("dev:" + a.id).c_str(), a.name.c_str());
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(audioCombo_), prevId.c_str());
    if (gtk_combo_box_get_active(GTK_COMBO_BOX(audioCombo_)) < 0) gtk_combo_box_set_active(GTK_COMBO_BOX(audioCombo_), 0);

    const gchar* prevOut = gtk_combo_box_get_active_id(GTK_COMBO_BOX(audioOutCombo_));
    std::string prevOutId = prevOut ? prevOut : settings_.getString("audio_out", "default");
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(audioOutCombo_));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(audioOutCombo_), "default", "Uscita predefinita di sistema");
    for (const auto& a : devices_.audioSinks())
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(audioOutCombo_), ("dev:" + a.id).c_str(), a.name.c_str());
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(audioOutCombo_), prevOutId.c_str());
    if (gtk_combo_box_get_active(GTK_COMBO_BOX(audioOutCombo_)) < 0) gtk_combo_box_set_active(GTK_COMBO_BOX(audioOutCombo_), 0);
    suppressSignals_ = false;
}

void AppWindow::populateOutputs() {
    suppressSignals_ = true;
    const gchar* prev = gtk_combo_box_get_active_id(GTK_COMBO_BOX(videoOutCombo_));
    std::string prevId = prev ? prev : settings_.getString("video_out", "");
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(videoOutCombo_));
    for (const auto& o : devices_.videoOutputs())
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(videoOutCombo_), o.path.c_str(), (o.name + (o.isLoopback ? " (virtuale)" : "")).c_str());
    if (devices_.videoOutputs().empty())
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(videoOutCombo_), "", "Nessuna uscita video: crea un dispositivo virtuale");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(videoOutCombo_), prevId.c_str());
    if (gtk_combo_box_get_active(GTK_COMBO_BOX(videoOutCombo_)) < 0) gtk_combo_box_set_active(GTK_COMBO_BOX(videoOutCombo_), 0);
    suppressSignals_ = false;
}

void AppWindow::populateChannels() {
    suppressSignals_ = true;
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(channelCombo_));
    auto channels = dvb::parseChannelsConf(channelsFile_);
    for (const auto& c : channels) gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(channelCombo_), c.name.c_str(), c.name.c_str());
    std::string last = settings_.getString("channel", "");
    if (!last.empty()) gtk_combo_box_set_active_id(GTK_COMBO_BOX(channelCombo_), last.c_str());
    if (gtk_combo_box_get_active(GTK_COMBO_BOX(channelCombo_)) < 0 && !channels.empty()) gtk_combo_box_set_active(GTK_COMBO_BOX(channelCombo_), 0);
    std::string txt = channels.empty() ? "Nessun canale: carica un channels.conf o esegui la scansione.\n" : std::to_string(channels.size()) + " canali da ";
    gtk_label_set_text(GTK_LABEL(channelsFileLabel_), (txt + channelsFile_).c_str());
    suppressSignals_ = false;
}

void AppWindow::onDevicesChanged() {
    std::vector<std::string> before;
    for (const auto& s : devices_.videoSources()) if (s.kind == SourceKind::Webcam) before.push_back(s.id);
    devices_.refresh();
    populateSources();
    populateAudioSources();
    populateOutputs();
    // Una webcam appena collegata, con nessuna sorgente attiva, viene selezionata e avviata da sola.
    const VideoSource* fresh = nullptr;
    for (const auto& s : devices_.videoSources())
        if (s.kind == SourceKind::Webcam && !s.info.isLoopback && std::find(before.begin(), before.end(), s.id) == before.end()) { fresh = &s; break; }
    if (fresh && !pipeline_.isRunning() && !pipeline_.isRecording()) {
        suppressSignals_ = true;
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(sourceCombo_), fresh->id.c_str());
        suppressSignals_ = false;
        setStatus("Nuova periferica rilevata: " + fresh->name);
    }
    onSourceChanged(pipeline_.isRunning() || fresh != nullptr);
    updateDeviceStatus();
    if (!fresh) setStatus(detectedDevicesText());
}

std::string AppWindow::detectedDevicesText() const {
    std::vector<std::string> names;
    int tv = 0;
    for (const auto& s : devices_.videoSources()) {
        if (s.kind == SourceKind::Webcam && !s.info.isLoopback) names.push_back(s.name);
        if (s.kind == SourceKind::Dvb) ++tv;
    }
    std::string t;
    if (names.empty()) t = "Nessuna webcam o scheda di acquisizione rilevata";
    else if (names.size() == 1) t = "Periferica di cattura: " + names[0];
    else {
        t = std::to_string(names.size()) + " periferiche di cattura: ";
        for (size_t i = 0; i < names.size(); ++i) t += (i ? ", " : "") + names[i];
    }
    if (tv) t += std::string(tv == 1 ? " · 1 scheda TV" : " · " + std::to_string(tv) + " schede TV");
    return t;
}

void AppWindow::updateDeviceStatus() {
    std::vector<std::string> names;
    for (const auto& s : devices_.videoSources()) if (s.kind == SourceKind::Webcam && !s.info.isLoopback) names.push_back(s.name);
    GtkStyleContext* ctx = gtk_widget_get_style_context(deviceLabel_);
    if (names.empty()) {
        gtk_label_set_text(GTK_LABEL(deviceLabel_), "Nessuna webcam rilevata.\nCollega la periferica e premi Aggiorna.");
        gtk_style_context_remove_class(ctx, "device-name");
        gtk_style_context_add_class(ctx, "dim");
    } else {
        std::string t = names.size() == 1 ? names[0] : std::to_string(names.size()) + " periferiche: ";
        if (names.size() > 1) for (size_t i = 0; i < names.size(); ++i) t += (i ? ", " : "") + names[i];
        gtk_label_set_text(GTK_LABEL(deviceLabel_), t.c_str());
        gtk_style_context_remove_class(ctx, "dim");
        gtk_style_context_add_class(ctx, "device-name");
    }
    // Segnaposto nell'area video (visibile solo quando nessuna sorgente è attiva)
    if (pipeline_.isRunning()) {
        gtk_widget_hide(placeholder_);
        return;
    }
    gtk_label_set_text(GTK_LABEL(placeholderTitle_), names.empty() ? "Nessuna periferica di cattura rilevata" : detectedDevicesText().c_str());
    gtk_label_set_text(GTK_LABEL(placeholderText_), names.empty()
        ? "Collega la webcam (o la scheda TV) e premi «Cerca dispositivi». Le webcam UVC come la Logitech C920 "
          "non richiedono driver aggiuntivi. In alternativa scegli la sorgente di prova o un URL nella pagina Sorgente."
        : "Premi «Avvia sorgente» per vedere l'anteprima, oppure scegli un'altra sorgente nella pagina Sorgente.");
    gtk_widget_show(placeholder_);
}

/* ------------------------------------------------------------- sorgente */

const VideoSource* AppWindow::currentSource() const {
    const gchar* id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(sourceCombo_));
    if (!id) return nullptr;
    for (const auto& s : devices_.videoSources())
        if (s.id == id) return &s;
    return nullptr;
}

void AppWindow::onSourceChanged(bool autoStart) {
    const VideoSource* s = currentSource();
    bool webcam = s && s->kind == SourceKind::Webcam;
    bool dvbSrc = s && s->kind == SourceKind::Dvb;
    bool uri = s && s->kind == SourceKind::Uri;
    gtk_widget_set_visible(formatRow_, webcam);
    gtk_widget_set_visible(uriRow_, uri);
    populateFormats();
    rebuildWebcamControls();
    if (dvbSrc) {
        std::string info = s->dvb.name + "\nAdattatore " + std::to_string(s->dvb.adapter) + ", frontend " + std::to_string(s->dvb.frontend);
        if (!s->dvb.delsys.empty()) {
            info += "\nSistemi: ";
            for (size_t i = 0; i < s->dvb.delsys.size(); ++i) info += (i ? ", " : "") + s->dvb.delsys[i];
        }
        gtk_label_set_text(GTK_LABEL(tvInfo_), info.c_str());
        gtk_stack_set_visible_child_name(GTK_STACK(stack_), "tv");
    } else {
        gtk_label_set_text(GTK_LABEL(tvInfo_), "Nessuna scheda TV (DVB) selezionata.");
    }
    if (webcam) gtk_stack_set_visible_child_name(GTK_STACK(stack_), "webcam");
    if (s) {
        settings_.set("source", s->id);
        settings_.set("source_name", s->kind == SourceKind::Webcam ? s->name : s->label());
    }
    if (pipeline_.isRunning() || (autoStart && s && s->kind != SourceKind::Uri && s->kind != SourceKind::Dvb)) startSource();
}

void AppWindow::restartIfRunning() {
    if (pipeline_.isRunning()) startSource();
}

void AppWindow::startSource() {
    const VideoSource* s = currentSource();
    if (!s) return;
    if (pipeline_.isRecording()) {
        showMessage("Ferma la registrazione prima di cambiare sorgente.", GTK_MESSAGE_WARNING);
        return;
    }
    SourceConfig cfg;
    cfg.kind = s->kind;
    const gchar* flip = gtk_combo_box_get_active_id(GTK_COMBO_BOX(flipCombo_));
    cfg.flip = flip ? flip : "none";
    switch (s->kind) {
        case SourceKind::Webcam: {
            cfg.devicePath = s->id;
            const gchar* fk = gtk_combo_box_get_active_id(GTK_COMBO_BOX(formatCombo_));
            if (fk && std::string(fk) != "auto") {
                for (const auto& f : s->formats) if (f.key() == fk) cfg.format = f;
                settings_.set(("format:" + s->id).c_str(), fk);
            }
            break;
        }
        case SourceKind::Dvb: {
            cfg.dvbAdapter = s->dvb.adapter;
            cfg.dvbFrontend = s->dvb.frontend;
            cfg.channelsConf = channelsFile_;
            const gchar* ch = gtk_combo_box_get_active_id(GTK_COMBO_BOX(channelCombo_));
            if (!manualTune_ && ch && *ch) {
                cfg.dvbChannel = ch;
                settings_.set("channel", ch);
            } else {
                const gchar* ds = gtk_combo_box_get_active_id(GTK_COMBO_BOX(delsysCombo_));
                cfg.delsys = ds ? ds : "dvb-t";
                cfg.frequencyMHz = gtk_spin_button_get_value(GTK_SPIN_BUTTON(freqSpin_));
                const gchar* bw = gtk_combo_box_get_active_id(GTK_COMBO_BOX(bandwidthCombo_));
                cfg.bandwidthMHz = bw ? int(std::lround(atof(bw))) : 8;
                cfg.symbolRateKBd = int(gtk_spin_button_get_value(GTK_SPIN_BUTTON(symbolSpin_)));
                const gchar* pol = gtk_combo_box_get_active_id(GTK_COMBO_BOX(polarityCombo_));
                cfg.polarity = pol ? pol : "h";
                if (!manualTune_ && !(ch && *ch)) {
                    showMessage("Nessun canale: carica un channels.conf, esegui la scansione oppure usa la sintonia manuale.", GTK_MESSAGE_INFO);
                    gtk_stack_set_visible_child_name(GTK_STACK(stack_), "tv");
                    return;
                }
            }
            break;
        }
        case SourceKind::Uri: {
            cfg.uri = gtk_entry_get_text(GTK_ENTRY(uriEntry_));
            if (cfg.uri.empty()) {
                showMessage("Inserisci un URL (rtsp://, http://, file:///…) e premi Invio.", GTK_MESSAGE_INFO);
                return;
            }
            settings_.set("uri", cfg.uri);
            break;
        }
        default: break;
    }

    AudioConfig audio;
    const gchar* aid = gtk_combo_box_get_active_id(GTK_COMBO_BOX(audioCombo_));
    std::string a = aid ? aid : "auto";
    settings_.set("audio", a);
    if (a == "none") {
        audio.enabled = false;
    } else if (a == "source") {
        audio.fromSource = true;
    } else if (a == "auto") {
        if (s->kind == SourceKind::Dvb || s->kind == SourceKind::Uri) {
            audio.fromSource = true;
        } else if (s->kind == SourceKind::Webcam) {
            int idx = devices_.findMatchingAudioSource(s->name);
            if (idx >= 0) audio.device = devices_.audioSources()[idx].device;   // microfono della webcam
        }
    } else if (a.rfind("dev:", 0) == 0) {
        for (const auto& d : devices_.audioSources()) if ("dev:" + d.id == a) audio.device = d.device;
    }

    std::string err;
    gtk_widget_hide(videoInfoLabel_);
    if (!pipeline_.start(cfg, audio, err)) {
        showMessage(err, GTK_MESSAGE_ERROR);
        updateSubtitle();
        updateDeviceStatus();
        return;
    }
    pipeline_.setMicVolume(gtk_range_get_value(GTK_RANGE(micScale_)));
    gtk_widget_hide(placeholder_);
    gtk_button_set_image(GTK_BUTTON(startButton_), gtk_image_new_from_icon_name("media-playback-stop-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_info_bar_set_revealed(GTK_INFO_BAR(infoBar_), FALSE);
    setStatus("In riproduzione: " + s->label());
    updateSubtitle();

    // Riattiva le uscite ponte se erano attive
    if (gtk_switch_get_active(GTK_SWITCH(videoOutSwitch_))) onVideoOutputToggled();
    if (gtk_switch_get_active(GTK_SWITCH(audioOutSwitch_))) onAudioOutputToggled();
    if (gtk_switch_get_active(GTK_SWITCH(extWinSwitch_))) onExternalWindowToggled();
}

void AppWindow::stopSource() {
    if (pipeline_.isRecording()) {
        showMessage("Ferma prima la registrazione.", GTK_MESSAGE_WARNING);
        return;
    }
    pipeline_.stop();
    gtk_button_set_image(GTK_BUTTON(startButton_), gtk_image_new_from_icon_name("media-playback-start-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_widget_hide(videoInfoLabel_);
    gtk_level_bar_set_value(GTK_LEVEL_BAR(levelBar_), 0);
    setStatus("Sorgente fermata.");
    updateSubtitle();
    updateDeviceStatus();
}

void AppWindow::updateSubtitle() {
    const VideoSource* s = currentSource();
    std::string sub = pipeline_.isRunning() && s ? (s->kind == SourceKind::Webcam ? s->name : s->label()) : "Nessuna sorgente";
    if (pipeline_.isRunning() && !lastVideoInfo_.empty()) sub += "  ·  " + lastVideoInfo_;
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(headerBar_), sub.c_str());
}

/* ------------------------------------------------------- controlli webcam */

void AppWindow::rebuildWebcamControls() {
    for (auto& cw : ctrlWidgets_) gtk_widget_destroy(cw.rowBox);
    ctrlWidgets_.clear();
    ctrlDev_.close();
    const VideoSource* s = currentSource();
    if (!s || s->kind != SourceKind::Webcam) {
        gtk_label_set_text(GTK_LABEL(webcamInfo_), "Nessuna webcam selezionata.");
        return;
    }
    std::string info = s->name + "\nDriver: " + s->info.driver + "  ·  " + s->info.busInfo;
    if (s->info.driver == "uvcvideo") info += "\nWebcam UVC riconosciuta dal kernel: nessun driver aggiuntivo necessario.";
    gtk_label_set_text(GTK_LABEL(webcamInfo_), info.c_str());
    if (!ctrlDev_.open(s->id)) return;

    for (const auto& c : ctrlDev_.enumerateControls()) {
        CtrlWidget cw;
        cw.ctrl = c;
        GtkWidget* w = nullptr;
        uint32_t id = c.id;
        switch (c.type) {
            case V4l2Control::Integer: {
                double step = double(c.step);
                w = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, double(c.min), double(c.max), step);
                gtk_scale_set_digits(GTK_SCALE(w), 0);
                gtk_scale_set_value_pos(GTK_SCALE(w), GTK_POS_RIGHT);
                gtk_scale_add_mark(GTK_SCALE(w), double(c.def), GTK_POS_BOTTOM, nullptr);
                gtk_range_set_value(GTK_RANGE(w), double(c.value));
                g_object_set_data(G_OBJECT(w), "ctrl-id", GUINT_TO_POINTER(id));
                g_signal_connect(w, "value-changed", G_CALLBACK(+[](GtkRange* r, gpointer self) {
                    if (SELF->updatingControls_) return;
                    uint32_t cid = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(r), "ctrl-id"));
                    SELF->onWebcamControlChanged(cid, int64_t(std::llround(gtk_range_get_value(r))));
                }), this);
                break;
            }
            case V4l2Control::Boolean: {
                w = gtk_switch_new();
                gtk_widget_set_halign(w, GTK_ALIGN_START);
                gtk_switch_set_active(GTK_SWITCH(w), c.value != 0);
                g_object_set_data(G_OBJECT(w), "ctrl-id", GUINT_TO_POINTER(id));
                g_signal_connect(w, "notify::active", G_CALLBACK(+[](GObject* o, GParamSpec*, gpointer self) {
                    if (SELF->updatingControls_) return;
                    uint32_t cid = GPOINTER_TO_UINT(g_object_get_data(o, "ctrl-id"));
                    SELF->onWebcamControlChanged(cid, gtk_switch_get_active(GTK_SWITCH(o)) ? 1 : 0);
                }), this);
                break;
            }
            case V4l2Control::Menu:
            case V4l2Control::IntegerMenu: {
                w = compactCombo();
                for (const auto& m : c.menuItems)
                    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(w), std::to_string(m.first).c_str(), m.second.c_str());
                gtk_combo_box_set_active_id(GTK_COMBO_BOX(w), std::to_string(c.value).c_str());
                g_object_set_data(G_OBJECT(w), "ctrl-id", GUINT_TO_POINTER(id));
                g_signal_connect(w, "changed", G_CALLBACK(+[](GtkComboBox* cb, gpointer self) {
                    if (SELF->updatingControls_) return;
                    const gchar* v = gtk_combo_box_get_active_id(cb);
                    if (!v) return;
                    uint32_t cid = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(cb), "ctrl-id"));
                    SELF->onWebcamControlChanged(cid, atoll(v));
                }), this);
                break;
            }
            case V4l2Control::Button: {
                w = gtk_button_new_with_label(c.name.c_str());
                g_object_set_data(G_OBJECT(w), "ctrl-id", GUINT_TO_POINTER(id));
                g_signal_connect(w, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer self) {
                    uint32_t cid = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(b), "ctrl-id"));
                    SELF->onWebcamControlChanged(cid, 1);
                }), this);
                break;
            }
            default: break;
        }
        if (!w) continue;
        gtk_widget_set_sensitive(w, !c.inactive && !c.readOnly);
        cw.widget = w;
        cw.rowBox = row(webcamControlsBox_, c.type == V4l2Control::Button ? nullptr : c.name.c_str(), w);
        ctrlWidgets_.push_back(cw);
    }
    gtk_widget_show_all(webcamControlsBox_);
}

void AppWindow::refreshWebcamControlValues() {
    if (!ctrlDev_.isOpen()) return;
    updatingControls_ = true;
    auto fresh = ctrlDev_.enumerateControls();
    for (auto& cw : ctrlWidgets_) {
        for (const auto& f : fresh) {
            if (f.id != cw.ctrl.id) continue;
            cw.ctrl = f;
            gtk_widget_set_sensitive(cw.widget, !f.inactive && !f.readOnly);
            switch (f.type) {
                case V4l2Control::Integer: gtk_range_set_value(GTK_RANGE(cw.widget), double(f.value)); break;
                case V4l2Control::Boolean: gtk_switch_set_active(GTK_SWITCH(cw.widget), f.value != 0); break;
                case V4l2Control::Menu:
                case V4l2Control::IntegerMenu: gtk_combo_box_set_active_id(GTK_COMBO_BOX(cw.widget), std::to_string(f.value).c_str()); break;
                default: break;
            }
        }
    }
    updatingControls_ = false;
}

void AppWindow::onWebcamControlChanged(uint32_t id, int64_t value) {
    if (!ctrlDev_.setControl(id, value)) {
        setStatus("Il dispositivo ha rifiutato il valore (controllo inattivo?).");
    }
    // Un controllo "auto" può attivare/disattivare altri controlli: rileggi tutto.
    refreshWebcamControlValues();
}

bool AppWindow::setControlByName(const char* needle, int64_t value) {
    for (const auto& cw : ctrlWidgets_) {
        std::string n = cw.ctrl.name;
        for (auto& ch : n) ch = char(tolower(static_cast<unsigned char>(ch)));
        if (n.find(needle) != std::string::npos) return ctrlDev_.setControl(cw.ctrl.id, value);
    }
    return false;
}

void AppWindow::applyPreset(int preset) {
    if (!ctrlDev_.isOpen()) { setStatus("Seleziona prima una webcam."); return; }
    switch (preset) {
        case 0: ctrlDev_.resetControls(); setStatus("Controlli riportati ai valori predefiniti."); break;
        case 1:
            ctrlDev_.resetControls();
            setControlByName("power line", 1);          // 1 = 50 Hz
            setControlByName("auto exposure", 3);       // 3 = priorità apertura (auto) per UVC
            setControlByName("exposure, auto", 3);
            setControlByName("white balance", 1);
            setStatus("Profilo interni 50 Hz applicato.");
            break;
        case 2:
            setControlByName("focus, automatic", 0);
            setControlByName("focus automatic", 0);
            setStatus("Autofocus disattivato: regola la messa a fuoco manualmente.");
            break;
    }
    refreshWebcamControlValues();
}

/* --------------------------------------------------------------------- TV */

void AppWindow::chooseChannelsFile() {
    GtkFileChooserNative* dlg = gtk_file_chooser_native_new("Scegli channels.conf", GTK_WINDOW(window_), GTK_FILE_CHOOSER_ACTION_OPEN, "Apri", "Annulla");
    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        gchar* f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (f) {
            channelsFile_ = f;
            settings_.set("channels_file", channelsFile_);
            g_free(f);
            populateChannels();
        }
    }
    g_object_unref(dlg);
}

void AppWindow::runChannelScan() {
    const VideoSource* s = currentSource();
    if (!s || s->kind != SourceKind::Dvb) {
        showMessage("Seleziona prima una scheda TV (DVB) nella pagina Sorgente.", GTK_MESSAGE_INFO);
        return;
    }
    gchar* tool = g_find_program_in_path("dvbv5-scan");
    if (!tool) {
        showMessage("dvbv5-scan non trovato: installa il pacchetto dvb-tools (sudo apt install dvb-tools).", GTK_MESSAGE_WARNING);
        return;
    }
    GtkFileChooserNative* dlg = gtk_file_chooser_native_new("File di scansione iniziale (es. dvb-t/it-Roma)", GTK_WINDOW(window_),
                                                            GTK_FILE_CHOOSER_ACTION_OPEN, "Scansiona", "Annulla");
    auto dirs = dvb::initialScanDirs();
    if (!dirs.empty()) gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), dirs.front().c_str());
    std::string initial;
    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        gchar* f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (f) { initial = f; g_free(f); }
    }
    g_object_unref(dlg);
    if (initial.empty()) { g_free(tool); return; }

    if (pipeline_.isRunning() && s->kind == SourceKind::Dvb) stopSource();   // libera il frontend
    std::string out = settings_.configDir() + "/channels.conf";
    std::string adapter = std::to_string(s->dvb.adapter), frontend = std::to_string(s->dvb.frontend);
    const gchar* argv[] = { tool, "-a", adapter.c_str(), "-f", frontend.c_str(), "-o", out.c_str(), "-O", "DVBV5", initial.c_str(), nullptr };
    GError* err = nullptr;
    GSubprocess* proc = g_subprocess_newv(argv, GSubprocessFlags(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE), &err);
    g_free(tool);
    if (!proc) {
        showMessage(std::string("Impossibile avviare dvbv5-scan: ") + (err ? err->message : ""), GTK_MESSAGE_ERROR);
        if (err) g_error_free(err);
        return;
    }
    g_object_set_data_full(G_OBJECT(proc), "out-file", g_strdup(out.c_str()), g_free);
    setStatus("Scansione canali in corso (può richiedere alcuni minuti)…");
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(scanLog_)), "Scansione in corso…\n", -1);
    g_subprocess_communicate_utf8_async(proc, nullptr, nullptr, +[](GObject* src, GAsyncResult* res, gpointer self) {
        SELF->onScanFinished(src, res);
    }, this);
}

void AppWindow::onScanFinished(GObject* procObj, GAsyncResult* res) {
    GSubprocess* proc = G_SUBPROCESS(procObj);
    gchar* out = nullptr;
    GError* err = nullptr;
    g_subprocess_communicate_utf8_finish(proc, res, &out, nullptr, &err);
    std::string log = out ? out : "";
    if (err) { log += std::string("\n") + err->message; g_error_free(err); }
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(scanLog_)), log.c_str(), -1);
    const char* file = static_cast<const char*>(g_object_get_data(procObj, "out-file"));
    if (g_subprocess_get_successful(proc) && file && g_file_test(file, G_FILE_TEST_EXISTS)) {
        channelsFile_ = file;
        settings_.set("channels_file", channelsFile_);
        populateChannels();
        setStatus("Scansione completata.");
    } else {
        showMessage("Scansione terminata con errori: vedi il registro nella pagina TV.", GTK_MESSAGE_WARNING);
    }
    g_free(out);
    g_object_unref(proc);
}

/* ------------------------------------------------------- registrazione */

std::string AppWindow::recordFolder() const {
    gchar* f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(folderButton_));
    std::string r = f ? f : g_get_home_dir();
    g_free(f);
    return r;
}

void AppWindow::toggleRecording() {
    if (pipeline_.isRecording()) {
        gtk_widget_set_sensitive(recordButton_, FALSE);
        gtk_widget_set_sensitive(bigRecordButton_, FALSE);
        setStatus("Finalizzazione del file…");
        pipeline_.stopRecording([this] {
            if (recordTimer_) { g_source_remove(recordTimer_); recordTimer_ = 0; }
            gtk_widget_hide(recBadge_);
            gtk_widget_set_sensitive(recordButton_, TRUE);
            gtk_widget_set_sensitive(bigRecordButton_, TRUE);
            gtk_button_set_label(GTK_BUTTON(bigRecordButton_), "●  Avvia registrazione");
            gtk_style_context_remove_class(gtk_widget_get_style_context(recordButton_), "destructive-action");
            gtk_label_set_text(GTK_LABEL(lastFileLabel_), ("Ultimo file: " + currentRecordingPath_).c_str());
            setStatus("Registrazione salvata: " + currentRecordingPath_);
        });
        return;
    }
    if (!pipeline_.isRunning()) {
        showMessage("Avvia prima una sorgente.", GTK_MESSAGE_INFO);
        return;
    }
    MediaPipeline::RecordOptions opt;
    bool mp4 = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fmtMp4_));
    opt.format = mp4 ? MediaPipeline::RecordFormat::Mp4 : MediaPipeline::RecordFormat::Ogg;
    opt.videoKbps = int(gtk_spin_button_get_value(GTK_SPIN_BUTTON(videoKbps_)));
    opt.audioKbps = int(gtk_spin_button_get_value(GTK_SPIN_BUTTON(audioKbps_)));
    opt.withAudio = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(recAudioCheck_));
    std::string prefix = gtk_entry_get_text(GTK_ENTRY(prefixEntry_));
    if (prefix.empty()) prefix = "SmartVision";
    std::string path = recordFolder() + "/" + prefix + "-" + timestamp() + (mp4 ? ".mp4" : ".ogg");
    std::string err;
    if (!pipeline_.startRecording(path, opt, err)) {
        showMessage(err, GTK_MESSAGE_ERROR);
        return;
    }
    currentRecordingPath_ = path;
    saveSettings();
    gtk_widget_show(recBadge_);
    gtk_label_set_text(GTK_LABEL(recBadge_), "● REC 00:00:00");
    gtk_button_set_label(GTK_BUTTON(bigRecordButton_), "■  Ferma registrazione");
    gtk_style_context_add_class(gtk_widget_get_style_context(recordButton_), "destructive-action");
    recordTimer_ = g_timeout_add(500, recordTimerCb, this);
    std::string audioNote = opt.withAudio && !pipeline_.hasAudio() ? " (senza audio: nessuna sorgente audio)" : "";
    setStatus("Registrazione in corso: " + path + audioNote);
}

gboolean AppWindow::recordTimerCb(gpointer self) {
    if (!SELF->pipeline_.isRecording()) return G_SOURCE_CONTINUE;
    gtk_label_set_text(GTK_LABEL(SELF->recBadge_), ("● REC " + formatDuration(SELF->pipeline_.recordingElapsedUs())).c_str());
    return G_SOURCE_CONTINUE;
}

void AppWindow::takeSnapshot() {
    if (!pipeline_.isRunning()) {
        showMessage("Avvia prima una sorgente.", GTK_MESSAGE_INFO);
        return;
    }
    std::string prefix = gtk_entry_get_text(GTK_ENTRY(prefixEntry_));
    if (prefix.empty()) prefix = "SmartVision";
    std::string path = recordFolder() + "/" + prefix + "-" + timestamp() + ".png";
    std::string err;
    if (pipeline_.snapshot(path, err)) {
        gtk_label_set_text(GTK_LABEL(lastFileLabel_), ("Ultimo file: " + path).c_str());
        setStatus("Istantanea salvata: " + path);
    } else {
        showMessage(err, GTK_MESSAGE_ERROR);
    }
}

void AppWindow::openRecordFolder() {
    std::string uri = "file://" + recordFolder();
    gtk_show_uri_on_window(GTK_WINDOW(window_), uri.c_str(), GDK_CURRENT_TIME, nullptr);
}

/* ----------------------------------------------------------------- uscite */

void AppWindow::onVideoOutputToggled() {
    bool on = gtk_switch_get_active(GTK_SWITCH(videoOutSwitch_));
    if (!on) { pipeline_.stopVideoOutput(); setStatus("Ponte video disattivato."); return; }
    const gchar* dev = gtk_combo_box_get_active_id(GTK_COMBO_BOX(videoOutCombo_));
    std::string err;
    if (!dev || !*dev) err = "Nessun dispositivo di uscita video. Crea un dispositivo virtuale v4l2loopback.";
    else if (dev == std::string(currentSource() ? currentSource()->id : "")) err = "Sorgente e uscita coincidono.";
    else if (pipeline_.startVideoOutput(dev, err)) {
        settings_.set("video_out", dev);
        setStatus(std::string("Ponte video attivo su ") + dev);
        return;
    }
    if (pipeline_.isRunning() || !err.empty()) {
        if (pipeline_.isRunning()) showMessage(err, GTK_MESSAGE_ERROR);
        suppressSignals_ = true;
        gtk_switch_set_active(GTK_SWITCH(videoOutSwitch_), FALSE);
        suppressSignals_ = false;
    }
}

void AppWindow::onAudioOutputToggled() {
    bool on = gtk_switch_get_active(GTK_SWITCH(audioOutSwitch_));
    if (!on) { pipeline_.stopAudioOutput(); setStatus("Ponte audio disattivato."); return; }
    const gchar* id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(audioOutCombo_));
    GstDevice* sink = nullptr;
    if (id && std::string(id).rfind("dev:", 0) == 0)
        for (const auto& d : devices_.audioSinks()) if ("dev:" + d.id == id) sink = d.device;
    std::string err;
    if (pipeline_.startAudioOutput(sink, err)) {
        settings_.set("audio_out", id ? id : "default");
        setStatus("Ponte audio attivo.");
        return;
    }
    if (pipeline_.isRunning()) showMessage(err, GTK_MESSAGE_ERROR);
    suppressSignals_ = true;
    gtk_switch_set_active(GTK_SWITCH(audioOutSwitch_), FALSE);
    suppressSignals_ = false;
}

void AppWindow::onExternalWindowToggled() {
    bool on = gtk_switch_get_active(GTK_SWITCH(extWinSwitch_));
    if (!on) { pipeline_.stopExternalWindow(); return; }
    std::string err;
    if (!pipeline_.startExternalWindow(err)) {
        if (pipeline_.isRunning()) showMessage(err, GTK_MESSAGE_ERROR);
        suppressSignals_ = true;
        gtk_switch_set_active(GTK_SWITCH(extWinSwitch_), FALSE);
        suppressSignals_ = false;
    }
}

void AppWindow::createLoopbackDevice() {
    if (g_file_test("/sys/module/v4l2loopback", G_FILE_TEST_IS_DIR)) {
        bool any = false;
        for (const auto& o : devices_.videoOutputs()) any = any || o.isLoopback;
        if (any) { showMessage("Il modulo v4l2loopback è già caricato: usa il dispositivo virtuale nell'elenco.", GTK_MESSAGE_INFO); return; }
    }
    if (!g_file_test("/sys/module/v4l2loopback", G_FILE_TEST_IS_DIR)) {
        gchar* mp = g_find_program_in_path("modinfo");
        bool hasModule = false;
        if (mp) {
            gchar* outp = nullptr;
            gint status = 0;
            const gchar* argv[] = { mp, "-n", "v4l2loopback", nullptr };
            if (g_spawn_sync(nullptr, const_cast<gchar**>(argv), nullptr, G_SPAWN_STDERR_TO_DEV_NULL, nullptr, nullptr, &outp, nullptr, &status, nullptr))
                hasModule = status == 0 && outp && *outp;
            g_free(outp);
            g_free(mp);
        }
        if (!hasModule) {
            showMessage("Modulo v4l2loopback non installato: sudo apt install v4l2loopback-dkms", GTK_MESSAGE_WARNING);
            return;
        }
    }
    const gchar* argv[] = { "pkexec", "modprobe", "v4l2loopback", "devices=1", "video_nr=10",
                            "card_label=SmartVision Output", "exclusive_caps=1", nullptr };
    GError* err = nullptr;
    GSubprocess* proc = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_NONE, &err);
    if (!proc) {
        showMessage(std::string("Impossibile eseguire pkexec: ") + (err ? err->message : ""), GTK_MESSAGE_ERROR);
        if (err) g_error_free(err);
        return;
    }
    setStatus("Caricamento di v4l2loopback (autenticazione richiesta)…");
    g_subprocess_wait_async(proc, nullptr, +[](GObject* src, GAsyncResult* res, gpointer self) {
        GSubprocess* p = G_SUBPROCESS(src);
        g_subprocess_wait_finish(p, res, nullptr);
        if (g_subprocess_get_successful(p)) {
            SELF->setStatus("Dispositivo virtuale creato (/dev/video10).");
            g_timeout_add(800, +[](gpointer s) -> gboolean { static_cast<AppWindow*>(s)->onDevicesChanged(); return G_SOURCE_REMOVE; }, self);
        } else {
            SELF->showMessage("Creazione del dispositivo virtuale annullata o fallita.", GTK_MESSAGE_WARNING);
        }
        g_object_unref(p);
    }, this);
}

/* ------------------------------------------------------------------ varie */

void AppWindow::toggleFullscreen() {
    fullscreen_ = !fullscreen_;
    if (fullscreen_) gtk_window_fullscreen(GTK_WINDOW(window_)); else gtk_window_unfullscreen(GTK_WINDOW(window_));
    gtk_widget_set_visible(sidebar_, !fullscreen_);
    gtk_widget_set_visible(bottomBar_, !fullscreen_);
}

void AppWindow::showAbout() {
    gtk_show_about_dialog(GTK_WINDOW(window_),
                          "program-name", APP_TITLE,
                          "version", APP_VERSION,
                          "comments", "Anteprima, registrazione (MP4/OGG) e ponte del segnale per webcam UVC, schede TV DVB e flussi di rete.\n"
                                      "Video: kernel uvcvideo / V4L2 · Audio: snd-usb-audio / PipeWire · Motore: GStreamer.",
                          "logo-icon-name", APP_ID,
                          "license-type", GTK_LICENSE_GPL_3_0,
                          "website", "https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/v4l2.html",
                          "website-label", "API V4L2 del kernel Linux",
                          nullptr);
}

void AppWindow::setStatus(const std::string& text) {
    gtk_label_set_text(GTK_LABEL(statusLabel_), text.c_str());
}

void AppWindow::showMessage(const std::string& text, GtkMessageType type) {
    gtk_label_set_text(GTK_LABEL(infoLabel_), text.c_str());
    gtk_info_bar_set_message_type(GTK_INFO_BAR(infoBar_), type);
    gtk_widget_show_all(infoBar_);
    gtk_info_bar_set_revealed(GTK_INFO_BAR(infoBar_), TRUE);
    setStatus(text);
}

void AppWindow::saveSettings() {
    settings_.set("record_format", gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fmtMp4_)) ? "mp4" : "ogg");
    settings_.set("video_kbps", int(gtk_spin_button_get_value(GTK_SPIN_BUTTON(videoKbps_))));
    settings_.set("audio_kbps", int(gtk_spin_button_get_value(GTK_SPIN_BUTTON(audioKbps_))));
    settings_.set("record_audio", gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(recAudioCheck_)) != FALSE);
    settings_.set("record_folder", recordFolder());
    settings_.set("prefix", std::string(gtk_entry_get_text(GTK_ENTRY(prefixEntry_))));
    const gchar* flip = gtk_combo_box_get_active_id(GTK_COMBO_BOX(flipCombo_));
    settings_.set("flip", flip ? flip : "none");
    settings_.save();
}

gboolean AppWindow::keyPressCb(GtkWidget*, GdkEventKey* e, gpointer self) {
    if (e->keyval == GDK_KEY_Escape && SELF->fullscreen_) { SELF->toggleFullscreen(); return TRUE; }
    return FALSE;
}

gboolean AppWindow::deleteCb(GtkWidget*, GdkEvent*, gpointer self) {
    SELF->saveSettings();
    if (SELF->pipeline_.isRecording()) {
        // Chiude il file correttamente prima di uscire.
        SELF->pipeline_.stopRecording([self] { gtk_widget_destroy(SELF->window_); });
        return TRUE;
    }
    SELF->pipeline_.stop();
    return FALSE;
}

/* -------------------------------------------------------------- auto-test */

void AppWindow::runSelfTest(const std::string& outDir) {
    selfTestDir_ = outDir;
    selfTestStep_ = 0;
    gtk_stack_set_transition_type(GTK_STACK(stack_), GTK_STACK_TRANSITION_TYPE_NONE);
    g_mkdir_with_parents(outDir.c_str(), 0755);
    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(folderButton_), outDir.c_str());
    gtk_entry_set_text(GTK_ENTRY(prefixEntry_), "selftest");
    suppressSignals_ = true;
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(sourceCombo_), "test");
    suppressSignals_ = false;
    startSource();
    g_timeout_add(1500, selfTestStep, this);
}

gboolean AppWindow::selfTestStep(gpointer self) {
    AppWindow* w = SELF;
    int step = w->selfTestStep_++;
    auto log = [](const std::string& m) { g_printerr("[self-test] %s\n", m.c_str()); };
    switch (step) {
        case 0:
            log(w->pipeline_.isRunning() ? "sorgente di prova avviata" : "ERRORE: sorgente non avviata");
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->fmtMp4_), TRUE);
            w->toggleRecording();
            log(w->pipeline_.isRecording() ? "registrazione MP4 avviata" : "ERRORE: registrazione MP4 non avviata");
            return G_SOURCE_CONTINUE;
        case 1: {                                                  // ~3 s di registrazione: intanto salva le schermate
            auto capture = [w, &log](const char* page, const char* file) {
                if (page) gtk_stack_set_visible_child_name(GTK_STACK(w->stack_), page);
                while (gtk_events_pending()) gtk_main_iteration_do(FALSE);
                GdkWindow* gw = gtk_widget_get_window(w->window_);
                if (!gw) return;
                GdkPixbuf* pb = gdk_pixbuf_get_from_window(gw, 0, 0, gdk_window_get_width(gw), gdk_window_get_height(gw));
                if (!pb) return;
                gdk_pixbuf_save(pb, (w->selfTestDir_ + "/" + file).c_str(), "png", nullptr, nullptr);
                g_object_unref(pb);
                log(std::string("schermata salvata: ") + file);
            };
            capture(nullptr, "schermata.png");
            capture("webcam", "schermata-webcam.png");
            capture("tv", "schermata-tv.png");
            capture("record", "schermata-registra.png");
            capture("output", "schermata-uscite.png");
            capture("source", "schermata-sorgente.png");
            return G_SOURCE_CONTINUE;
        }
        case 2: return G_SOURCE_CONTINUE;
        case 3:
            w->toggleRecording();
            log("stop MP4 richiesto");
            return G_SOURCE_CONTINUE;
        case 4:
            log(w->pipeline_.isRecording() ? "ERRORE: MP4 non finalizzato" : "MP4 finalizzato");
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->fmtOgg_), TRUE);
            w->toggleRecording();
            log(w->pipeline_.isRecording() ? "registrazione OGG avviata" : "ERRORE: registrazione OGG non avviata");
            return G_SOURCE_CONTINUE;
        case 5: case 6: return G_SOURCE_CONTINUE;
        case 7:
            w->toggleRecording();
            log("stop OGG richiesto");
            return G_SOURCE_CONTINUE;
        case 8: {
            log(w->pipeline_.isRecording() ? "ERRORE: OGG non finalizzato" : "OGG finalizzato");
            w->takeSnapshot();
            std::string err;
            log(w->pipeline_.startAudioOutput(nullptr, err) ? "ponte audio attivo" : "ponte audio non attivo: " + err);
            log(w->pipeline_.startExternalWindow(err) ? "finestra esterna aperta" : "finestra esterna non aperta: " + err);
            return G_SOURCE_CONTINUE;
        }
        case 9:
            w->pipeline_.stopAudioOutput();
            w->pipeline_.stopExternalWindow();
            w->stopSource();   // mostra il segnaposto "periferica" per la schermata finale
            return G_SOURCE_CONTINUE;
        case 10: {
            while (gtk_events_pending()) gtk_main_iteration_do(FALSE);
            GdkWindow* gw = gtk_widget_get_window(w->window_);
            if (gw) {
                GdkPixbuf* pb = gdk_pixbuf_get_from_window(gw, 0, 0, gdk_window_get_width(gw), gdk_window_get_height(gw));
                if (pb) {
                    gdk_pixbuf_save(pb, (w->selfTestDir_ + "/schermata-nessuna-periferica.png").c_str(), "png", nullptr, nullptr);
                    g_object_unref(pb);
                    log("schermata salvata: schermata-nessuna-periferica.png");
                }
            }
            return G_SOURCE_CONTINUE;
        }
        default:
            log("completato");
            w->saveSettings();
            w->pipeline_.stop();
            gtk_widget_destroy(w->window_);
            return G_SOURCE_REMOVE;
    }
}
