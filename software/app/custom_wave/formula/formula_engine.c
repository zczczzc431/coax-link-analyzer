#include "formula_engine.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct parser { const char *s; size_t pos, nodes; unsigned depth; enum cf_error error; double t; };
#define CF_PI 3.141592653589793238462643383279502884
#define CF_PERIODIC_TOLERANCE 1e-9
#define CF_MAX_NORMALIZED_STEP 0.60

static void ws(struct parser *p) { while (isspace((unsigned char)p->s[p->pos])) ++p->pos; }
static int take(struct parser *p, char c) { ws(p); if (p->s[p->pos] == c) { ++p->pos; return 1; } return 0; }
static double expr(struct parser *p);
static double primary(struct parser *p) {
    char name[8]; size_t n = 0; char *end; double v, a, b;
    if (p->error) return 0;
    ws(p); if (++p->nodes > 128u) { p->error = CF_ERR_NODES; return 0; }
    if (p->depth++ > 16u) { p->error = CF_ERR_DEPTH; --p->depth; return 0; }
    if (take(p, '(')) { v = expr(p); if (!take(p, ')') && !p->error) p->error = CF_ERR_SYNTAX; --p->depth; return v; }
    if (p->s[p->pos] == '+' || p->s[p->pos] == '-') { int neg = p->s[p->pos++] == '-'; v = primary(p); --p->depth; return neg ? -v : v; }
    if (isdigit((unsigned char)p->s[p->pos]) || p->s[p->pos] == '.') {
        errno = 0; v = strtod(p->s + p->pos, &end); if (end == p->s + p->pos || errno == ERANGE) p->error = CF_ERR_SYNTAX; else p->pos = (size_t)(end - p->s); --p->depth; return v;
    }
    while (isalpha((unsigned char)p->s[p->pos]) && n < sizeof(name)-1) {
        name[n++] = p->s[p->pos++];
    }
    name[n] = 0;
    if (!n) { p->error = !p->s[p->pos] || p->s[p->pos] == ')' || p->s[p->pos] == ',' ? CF_ERR_SYNTAX : CF_ERR_CHAR; --p->depth; return 0; }
    if (!strcmp(name, "t")) { --p->depth; return p->t; }
    if (!strcmp(name, "pi")) { --p->depth; return CF_PI; }
    if (!take(p, '(')) { p->error = CF_ERR_SYNTAX; --p->depth; return 0; }
    v = expr(p);
    if (p->error) { --p->depth; return 0; }
    if (!strcmp(name, "min") || !strcmp(name, "max") || !strcmp(name, "clamp")) {
        if (!take(p, ',') && !p->error) p->error = CF_ERR_SYNTAX;
        a = expr(p);
        if (!strcmp(name, "clamp")) {
            if (!take(p, ',') && !p->error) p->error = CF_ERR_SYNTAX;
            b = expr(p);
            if (!take(p, ')') && !p->error) p->error = CF_ERR_SYNTAX;
            if (!p->error) {
                if (a > b) p->error = CF_ERR_RANGE;
                else v = v < a ? a : (v > b ? b : v);
            }
        } else {
            if (!take(p, ')') && !p->error) p->error = CF_ERR_SYNTAX;
            if (!p->error) v = !strcmp(name, "min") ? fmin(v, a) : fmax(v, a);
        }
    } else {
        if (!take(p, ')') && !p->error) p->error = CF_ERR_SYNTAX;
        if (!strcmp(name, "sin")) v = sin(v); else if (!strcmp(name, "cos")) v = cos(v); else if (!strcmp(name, "abs")) v = fabs(v); else p->error = CF_ERR_CHAR;
    }
    --p->depth; return v;
}
static double product(struct parser *p) {
    double v = primary(p);
    while (!p->error) {
        if (!isfinite(v)) { p->error = CF_ERR_NONFINITE; break; }
        if (take(p, '*')) v *= primary(p);
        else if (take(p, '/')) {
            double d = primary(p);
            if (p->error) break;
            if (!isfinite(d)) p->error = CF_ERR_NONFINITE;
            else if (d == 0.0) p->error = CF_ERR_DIVZERO;
            else v /= d;
        } else break;
    }
    return v;
}
static double expr(struct parser *p) {
    double v = product(p);
    while (!p->error) {
        if (!isfinite(v)) { p->error = CF_ERR_NONFINITE; break; }
        if (take(p, '+')) v += product(p);
        else if (take(p, '-')) v -= product(p);
        else break;
    }
    return v;
}

const char *cf_error_string(enum cf_error e) { static const char *s[] = {"ok","empty","character","syntax","depth","nodes","divide-by-zero","non-finite","range","periodic","slope"}; return (unsigned)e <= CF_ERR_SLOPE ? s[e] : "unknown"; }
uint32_t cf_crc32(const void *data, size_t length) { uint32_t c = 0xffffffffu; const uint8_t *p = (const uint8_t *)data; while (length--) { c ^= *p++; for (unsigned i=0;i<8;i++) c = (c >> 1) ^ (0xedb88320u & -(c & 1u)); } return ~c; }

enum cf_error cf_generate(const char *s, struct cf_table *out) {
    if (!s || !out || !*s) return CF_ERR_EMPTY;
    size_t len = strlen(s);
    if (len > CF_MAX_EXPR) return CF_ERR_RANGE;
    double raw[CF_TABLE_LEN], lo = HUGE_VAL, hi = -HUGE_VAL; struct parser p;
    for (size_t i=0;i<CF_TABLE_LEN;i++) { p=(struct parser){s,0,0,0,CF_OK,(double)i/(double)CF_TABLE_LEN}; raw[i]=expr(&p); ws(&p); if (p.error || s[p.pos]) return p.error ? p.error : CF_ERR_SYNTAX; if (!isfinite(raw[i])) return CF_ERR_NONFINITE; if(raw[i]<lo)lo=raw[i]; if(raw[i]>hi)hi=raw[i]; }
    double peak=fmax(fabs(lo),fabs(hi)); double scale=peak>1.0?1.0/peak:1.0;
    /* t=1 is evaluated only for closure, never stored as a duplicated sample. */
    p=(struct parser){s,0,0,0,CF_OK,1.0};
    double end=expr(&p);
    if(p.error) return p.error;
    if(!isfinite(end)) return CF_ERR_NONFINITE;
    if(fabs(end*scale-raw[0]*scale) > CF_PERIODIC_TOLERANCE) return CF_ERR_PERIODIC;
    {
        double max_step = 0.0;
        for (size_t i=0; i<CF_TABLE_LEN; ++i) {
            double step = fabs(raw[i]*scale - raw[(i+CF_TABLE_LEN-1)%CF_TABLE_LEN]*scale);
            if (step > max_step) max_step = step;
        }
        if (max_step > CF_MAX_NORMALIZED_STEP) return CF_ERR_SLOPE;
    }
    for(size_t i=0;i<CF_TABLE_LEN;i++){ double q=raw[i]*scale*32767.0; if(q>32767)q=32767; if(q<-32768)q=-32768; out->q15[i]=(int16_t)lrint(q); }
    out->minimum=lo; out->maximum=hi; out->scale=scale; out->crc32=cf_crc32(out->q15,sizeof(out->q15)); return CF_OK;
}
