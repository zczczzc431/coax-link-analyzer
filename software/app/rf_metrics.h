#ifndef RF_METRICS_H
#define RF_METRICS_H
#include <stddef.h>
struct rf_metrics_result {
    int valid;
    unsigned fundamental_bin, second_bin, third_bin, spur_bin;
    double fundamental_code, hd2_db, hd3_db, thd_percent;
    double sfdr_db, snr_db, noise_floor_db;
    double occupied_bw_hz, bin_hz;
};
/* Analyze a one-sided magnitude spectrum in linear code units.  The caller
 * supplies the already-windowed/normalized FFT magnitude, so this routine
 * does not invent ADC volts or absolute RF power. */
int rf_metrics_analyze(const double *code, size_t bins, double fs_hz,
                       unsigned fundamental_bin, struct rf_metrics_result *out);
#endif
