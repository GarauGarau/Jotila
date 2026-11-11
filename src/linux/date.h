/* Dichiarazioni per la gestione delle date. */
/* Limiti di anno supportati: 1601-9999. */

#ifndef JOTILLA_DATE_H
#define JOTILLA_DATE_H

#define DATE_MIN_YEAR 1601
#define DATE_MAX_YEAR 9999

int date_days(int year, int month);
int date_valid(unsigned date);
int date_ordinal(unsigned date);
int date_weekday(unsigned date);
unsigned date_shift(unsigned date, int days);
unsigned date_parse(const char *value);
char *date_format(unsigned date);

#endif
