/* Gestione date in formato YYYYMMDD con validazione e aritmetica. */
/* Supporta anni dal 1601 al 9999. */

#include <glib.h>
#include "date.h"

/* Giorni del mese, con gestione dell'anno bisestile per febbraio. */
int date_days(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (year < DATE_MIN_YEAR || year > DATE_MAX_YEAR || month < 1 || month > 12) return 0;
    return days[month - 1] +
           (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

/* Verifica che il giorno sia valido per anno e mese codificati nella data. */
int date_valid(unsigned date) {
    int day = (int)(date % 100);
    return day > 0 && day <= date_days((int)(date / 10000), (int)(date / 100 % 100));
}

/* Giorno dell'anno (0-based) con correzione per anno bisestile. */
int date_ordinal(unsigned date) {
    static const int starts[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int year = (int)(date / 10000), month = (int)(date / 100 % 100);
    if (month < 1 || month > 12 || !date_valid(date)) return -1;
    return starts[month - 1] + (int)(date % 100) - 1 +
           (month > 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

/* Calcola il giorno della settimana (0=domenica) dalla data. */
int date_weekday(unsigned date) {
    int year = (int)(date / 10000) - 1, ordinal = date_ordinal(date);
    return ordinal < 0 ? -1
                       : (year * 365 + year / 4 - year / 100 + year / 400 + ordinal) % 7;
}

/* Sposta la data di N giorni, saturando ai limiti supportati. */
unsigned date_shift(unsigned date, int days) {
    int year = (int)(date / 10000), month = (int)(date / 100 % 100), day = (int)(date % 100);
    if (!date_valid(date)) return 0;
    while (days > 0) {
        int left = date_days(year, month) - day;
        if (days <= left) { day += days; break; }
        if (year == DATE_MAX_YEAR && month == 12) return 99991231; /* Satura al massimo. */
        days -= left + 1; day = 1;
        if (++month > 12) { month = 1; ++year; }
    }
    while (days < 0) {
        if (days > -day) { day += days; break; }
        if (year == DATE_MIN_YEAR && month == 1) return 16010101; /* Satura al minimo. */
        days += day;
        if (--month < 1) { month = 12; --year; }
        day = date_days(year, month);
    }
    return (unsigned)(year * 10000 + month * 100 + day);
}

/* Analizza la stringa "YYYYMMDD" in valore unsigned; 0 se invalida. */
unsigned date_parse(const char *value) {
    unsigned date = 0;
    int i;
    if (!value) return 0;
    for (i = 0; i < 8; ++i) {
        if (value[i] < '0' || value[i] > '9') return 0;
        date = date * 10 + (unsigned)(value[i] - '0');
    }
    return value[8] == '\0' && date_valid(date) ? date : 0;
}

/* Formatta la data come "YYYY-MM-DD". */
char *date_format(unsigned date) {
    if (!date_valid(date)) return g_strdup("");
    return g_strdup_printf("%04u-%02u-%02u",
                           date / 10000, date / 100 % 100, date % 100);
}

