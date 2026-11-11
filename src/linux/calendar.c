/* Selettore data modale basato su GtkCalendar per Jotilla.
 * Espone un dialog con pulsanti "Clear"/"OK", vista mensile o annuale e
 * callback di scelta. */
#include "calendar.h"
#include "date.h"

#define YEAR_COLUMNS 3
#define YEAR_ROWS 4
#define YEAR_MONTHS (YEAR_COLUMNS * YEAR_ROWS)

typedef struct {
    GtkWidget *dialog;
    GtkWidget *stack;
    GtkWidget *single_calendar;
    GtkWidget *year_scroller;
    GtkWidget *year_calendars[YEAR_MONTHS];
    GtkWidget *year_toggle;
    GtkWidget *year_spin;
    gulong year_spin_handler;
    guint year;
    guint32 selected;
    gboolean year_mode;
    gboolean updating;
    void (*on_pick)(guint32 date, gpointer data);
    gpointer data;
} CalendarPicker;

static void calendar_apply_year(CalendarPicker *picker);

/* Nomi inglesi dei mesi usati come etichette nella vista annuale. */
static const char *const month_names[YEAR_MONTHS] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

/* Data odierna in formato AAAAMMGG (0 se non disponibile). */
static guint32 calendar_today(void) {
    GDateTime *now = g_date_time_new_now_local();
    guint32 date;

    if (now == NULL)
        return 0;
    date = (guint32)(g_date_time_get_year(now) * 10000 +
                     g_date_time_get_month(now) * 100 +
                     g_date_time_get_day_of_month(now));
    g_date_time_unref(now);
    return date;
}

/* Legge la data selezionata da un GtkCalendar, in formato AAAAMMGG. */
static guint32 calendar_get_date(GtkCalendar *calendar) {
    guint year = 0;
    guint month = 0;
    guint day = 0;

    gtk_calendar_get_date(calendar, &year, &month, &day);
    return (guint32)(year * 10000u + (month + 1u) * 100u + day);
}

/* Seleziona una data (AAAAMMGG) in un GtkCalendar. */
static void calendar_set_date(GtkCalendar *calendar, guint32 date) {
    guint year;
    guint month;
    guint day;

    if (calendar == NULL || !date_valid(date))
        return;
    year = date / 10000u;
    month = date / 100u % 100u;
    day = date % 100u;
    gtk_calendar_select_month(calendar, month - 1u, year);
    gtk_calendar_select_day(calendar, day);
}

/* Aggiorna lo spin dell'anno senza innescare il relativo callback. */
static void calendar_set_year_spin(CalendarPicker *picker, guint year) {
    if (picker->year_spin == NULL)
        return;
    if (year < DATE_MIN_YEAR)
        year = DATE_MIN_YEAR;
    if (year > DATE_MAX_YEAR)
        year = DATE_MAX_YEAR;
    if (picker->year_spin_handler != 0)
        g_signal_handler_block(picker->year_spin, picker->year_spin_handler);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(picker->year_spin),
                              (gdouble)year);
    if (picker->year_spin_handler != 0)
        g_signal_handler_unblock(picker->year_spin, picker->year_spin_handler);
}

/* Porta il selettore alla data odierna senza chiudere il dialogo. */
static void on_today_clicked(GtkButton *button, gpointer data) {
    CalendarPicker *picker = data;
    guint32 today = calendar_today();

    (void)button;
    if (!date_valid(today)) return;
    picker->selected = today;
    picker->year = today / 10000u;
    calendar_set_year_spin(picker, picker->year);
    if (picker->single_calendar != NULL)
        calendar_set_date(GTK_CALENDAR(picker->single_calendar), today);
    calendar_apply_year(picker);
}

/* Aggiorna tutti i mesi della vista annuale sull'anno e sulla data scelti. */
static void calendar_apply_year(CalendarPicker *picker) {
    guint selected_month = 1;
    guint selected_day = 1;
    guint year = picker->year;
    int month;

    if (date_valid(picker->selected)) {
        year = picker->selected / 10000u;
        selected_month = picker->selected / 100u % 100u;
        selected_day = picker->selected % 100u;
        picker->year = year;
    }
    if (year < DATE_MIN_YEAR || year > DATE_MAX_YEAR)
        year = DATE_MIN_YEAR;

    picker->updating = TRUE;
    for (month = 0; month < YEAR_MONTHS; ++month) {
        GtkCalendar *calendar = GTK_CALENDAR(picker->year_calendars[month]);
        gtk_calendar_select_month(calendar, (guint)month, year);
        gtk_calendar_select_day(calendar,
                                (guint)(month + 1) == selected_month
                                    ? selected_day
                                    : 1u);
    }
    picker->updating = FALSE;
}

/* Cambio anno dallo spin: aggiorna vista mensile e vista annuale. */
static void on_year_spin_changed(GtkSpinButton *spin, gpointer data) {
    CalendarPicker *picker = data;
    guint new_year;
    guint month;
    guint day;
    guint calendar_year = 0;
    guint calendar_month = 0;
    guint calendar_day = 1;
    int max_day;

    if (picker->updating)
        return;

    new_year = (guint)gtk_spin_button_get_value_as_int(spin);
    if (new_year < DATE_MIN_YEAR)
        new_year = DATE_MIN_YEAR;
    if (new_year > DATE_MAX_YEAR)
        new_year = DATE_MAX_YEAR;

    month = picker->selected / 100u % 100u;
    day = picker->selected % 100u;
    if (!date_valid(picker->selected) || month == 0 || day == 0) {
        gtk_calendar_get_date(GTK_CALENDAR(picker->single_calendar),
                              &calendar_year, &calendar_month, &calendar_day);
        month = calendar_month + 1u;
        day = calendar_day;
    }
    (void)calendar_year;
    if (month < 1u || month > 12u)
        month = 1u;

    max_day = date_days((int)new_year, (int)month);
    if (max_day < 1)
        max_day = 1;
    if (day < 1u)
        day = 1u;
    if (day > (guint)max_day)
        day = (guint)max_day;

    picker->year = new_year;
    picker->selected = new_year * 10000u + month * 100u + day;

    picker->updating = TRUE;
    gtk_calendar_select_month(GTK_CALENDAR(picker->single_calendar),
                              month - 1u, new_year);
    gtk_calendar_select_day(GTK_CALENDAR(picker->single_calendar), day);
    calendar_apply_year(picker);
    picker->updating = FALSE;
}

/* Selezione di un giorno nella vista annuale: aggiorna la data bersaglio. */
static void on_year_day_selected(GtkCalendar *calendar, gpointer data) {
    CalendarPicker *picker = data;
    guint32 date = calendar_get_date(calendar);

    if (picker->updating || !date_valid(date))
        return;

    picker->selected = date;
    picker->year = date / 10000u;
    calendar_set_year_spin(picker, picker->year);

    picker->updating = TRUE;
    calendar_set_date(GTK_CALENDAR(picker->single_calendar), date);
    picker->updating = FALSE;
}

/* Selezione di un giorno nella vista mensile: mantiene allineata la data. */
static void on_single_day_selected(GtkCalendar *calendar, gpointer data) {
    CalendarPicker *picker = data;
    guint32 date = calendar_get_date(calendar);

    if (picker->updating || !date_valid(date))
        return;
    picker->selected = date;
    picker->year = date / 10000u;
    calendar_set_year_spin(picker, picker->year);
}

/* Mostra la vista mensile o quella annuale in base al toggle "Year". */
static void on_year_toggled(GtkToggleButton *button, gpointer data) {
    CalendarPicker *picker = data;
    gboolean active = gtk_toggle_button_get_active(button);

    if (active == picker->year_mode)
        return;

    picker->year_mode = active;
    if (active) {
        if (date_valid(picker->selected))
            picker->year = picker->selected / 10000u;
        calendar_set_year_spin(picker, picker->year);
        calendar_apply_year(picker);
        gtk_stack_set_visible_child(GTK_STACK(picker->stack),
                                    picker->year_scroller);
    } else {
        picker->updating = TRUE;
        calendar_set_date(GTK_CALENDAR(picker->single_calendar),
                          picker->selected);
        picker->updating = FALSE;
        gtk_stack_set_visible_child(GTK_STACK(picker->stack),
                                    picker->single_calendar);
    }
}

/* Gestisce la risposta del dialog: restituisce la data scelta o 0 se annullata. */
static void calendar_picker_response(GtkDialog *dialog, gint response_id,
                                     gpointer data) {
    CalendarPicker *picker = data;

    if (response_id == GTK_RESPONSE_OK) {
        guint32 date = picker->year_mode
                           ? picker->selected
                           : calendar_get_date(GTK_CALENDAR(picker->single_calendar));

        if (!date_valid(date))
            date = 0; /* data non valida -> nessuna data */
        if (picker->on_pick)
            picker->on_pick(date, picker->data);
    } else if (response_id == GTK_RESPONSE_REJECT) {
        if (picker->on_pick)
            picker->on_pick(0, picker->data);
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

/* Esc chiude con CANCEL, Invio conferma con OK; le frecce restano a GtkCalendar. */
static gboolean calendar_picker_key_press(GtkWidget *widget, GdkEventKey *event,
                                          gpointer data) {
    CalendarPicker *picker = data;

    (void)widget;

    if (event->keyval == GDK_KEY_Escape) {
        gtk_dialog_response(GTK_DIALOG(picker->dialog), GTK_RESPONSE_CANCEL);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
        GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(picker->dialog));

        if (focus != NULL && focus == picker->year_spin)
            gtk_spin_button_update(GTK_SPIN_BUTTON(picker->year_spin));
        gtk_dialog_response(GTK_DIALOG(picker->dialog), GTK_RESPONSE_OK);
        return TRUE;
    }
    return FALSE;
}

/* Costruisce il contenitore scorrevole con la griglia 3x4 dei mesi. */
static GtkWidget *calendar_year_view_new(CalendarPicker *picker) {
    GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *grid = gtk_grid_new();
    int month;

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_widget_set_margin_start(grid, 8);
    gtk_widget_set_margin_end(grid, 8);
    gtk_widget_set_margin_top(grid, 8);
    gtk_widget_set_margin_bottom(grid, 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);

    for (month = 0; month < YEAR_MONTHS; ++month) {
        GtkWidget *cell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        GtkWidget *label = gtk_label_new(month_names[month]);
        GtkWidget *calendar = gtk_calendar_new();

        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_style_context_add_class(gtk_widget_get_style_context(label),
                                    "dim-label");
        gtk_calendar_set_display_options(GTK_CALENDAR(calendar),
                                         GTK_CALENDAR_SHOW_DAY_NAMES);
        gtk_box_pack_start(GTK_BOX(cell), label, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(cell), calendar, FALSE, FALSE, 0);
        gtk_grid_attach(GTK_GRID(grid), cell,
                        month % YEAR_COLUMNS, month / YEAR_COLUMNS, 1, 1);

        picker->year_calendars[month] = calendar;
        g_signal_connect(calendar, "day-selected",
                         G_CALLBACK(on_year_day_selected), picker);
    }

    gtk_container_add(GTK_CONTAINER(scroller), grid);
    return scroller;
}

/* Crea e configura il dialog calendario, preselezionando la data corrente se valida. */
GtkWidget *calendar_picker_new(GtkWindow *parent, guint32 current,
                               void (*on_pick)(guint32 date, gpointer data),
                               gpointer data) {
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *header;
    GtkWidget *calendar;
    CalendarPicker *picker;

    dialog = gtk_dialog_new_with_buttons("Choose date", parent,
                                         GTK_DIALOG_MODAL |
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "Clear", GTK_RESPONSE_REJECT,
                                         "OK", GTK_RESPONSE_OK,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    picker = g_new0(CalendarPicker, 1);
    picker->dialog = dialog;
    picker->on_pick = on_pick;
    picker->data = data;
    picker->selected = date_valid(current) ? current : calendar_today();
    if (date_valid(picker->selected))
        picker->year = picker->selected / 10000u;

    header = gtk_header_bar_new();
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "Choose date");
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    {
        GtkWidget *today = gtk_button_new_with_label("Today");

        gtk_widget_set_tooltip_text(today, "Go to today");
        g_signal_connect(today, "clicked", G_CALLBACK(on_today_clicked), picker);
        gtk_header_bar_pack_end(GTK_HEADER_BAR(header), today);
    }
    picker->year_toggle = gtk_toggle_button_new_with_label("Year");
    gtk_widget_set_tooltip_text(picker->year_toggle, "Show the whole year");
    if (picker->year < DATE_MIN_YEAR)
        picker->year = DATE_MIN_YEAR;
    picker->year_spin = gtk_spin_button_new_with_range(DATE_MIN_YEAR,
                                                       DATE_MAX_YEAR, 1);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(picker->year_spin), 0);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(picker->year_spin), TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(picker->year_spin), 5);
    gtk_widget_set_tooltip_text(picker->year_spin, "Select year");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(picker->year_spin),
                              (gdouble)picker->year);
    picker->year_spin_handler =
        g_signal_connect(picker->year_spin, "value-changed",
                         G_CALLBACK(on_year_spin_changed), picker);
    g_signal_connect(picker->year_spin, "key-press-event",
                     G_CALLBACK(calendar_picker_key_press), picker);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), picker->year_toggle);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), picker->year_spin);
    gtk_window_set_titlebar(GTK_WINDOW(dialog), header);

    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 8);

    picker->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(picker->stack),
                                  GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_widget_set_vexpand(picker->stack, TRUE);

    calendar = gtk_calendar_new();
    gtk_widget_set_halign(calendar, GTK_ALIGN_CENTER);
    picker->single_calendar = calendar;
    gtk_stack_add_named(GTK_STACK(picker->stack), calendar, "month");

    picker->year_scroller = calendar_year_view_new(picker);
    gtk_stack_add_named(GTK_STACK(picker->stack), picker->year_scroller,
                        "year");

    gtk_stack_set_visible_child_name(GTK_STACK(picker->stack), "month");
    gtk_box_pack_start(GTK_BOX(content), picker->stack, TRUE, TRUE, 0);

    picker->updating = TRUE;
    calendar_set_date(GTK_CALENDAR(calendar), picker->selected);
    picker->updating = FALSE;

    calendar_apply_year(picker);

    g_signal_connect(calendar, "day-selected",
                     G_CALLBACK(on_single_day_selected), picker);
    g_signal_connect(picker->year_toggle, "toggled",
                     G_CALLBACK(on_year_toggled), picker);
    g_signal_connect(dialog, "key-press-event",
                     G_CALLBACK(calendar_picker_key_press), picker);
    g_object_set_data_full(G_OBJECT(dialog), "calendar-picker", picker, g_free);
    g_signal_connect(dialog, "response",
                     G_CALLBACK(calendar_picker_response), picker);

    return dialog;
}
