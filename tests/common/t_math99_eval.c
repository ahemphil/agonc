/* t_math99_eval.c - lib/libc/math99.c and mathf.c, built by the PC's
 * compiler (names am_cbrt and so on: math_names.h), as an evaluator for
 * test_math.py's R test: each line read is a function's name and its
 * arguments as a double's bits in hex (a float form's arguments are
 * floats, exactly); each line written is the result's bits in hex, a
 * double's 16 digits or a float's 8. C99, for the PC only.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define D1(n) double am_##n(double);
#define D2(n) double am_##n(double, double);
#define F1(n) float am_##n##f(float);
#define F2(n) float am_##n##f(float, float);
D1(cbrt) D1(log1p) D1(expm1) D1(log2) D1(exp2) D1(asinh) D1(acosh) D1(atanh) D1(erf) D1(erfc) D1(lgamma)
D1(tgamma) D2(hypot)
F1(sin) F1(cos) F1(tan) F1(asin) F1(acos) F1(atan) F1(exp) F1(log) F1(log10) F1(sqrt) F1(sinh) F1(cosh)
F1(tanh) F1(cbrt) F1(exp2) F1(log2) F1(erf) F1(erfc) F1(lgamma) F1(tgamma) F1(log1p) F1(expm1) F1(asinh)
F1(acosh) F1(atanh) F2(pow) F2(atan2) F2(hypot)

struct d1 {
    const char *name;
    double (*f)(double);
};

struct f1 {
    const char *name;
    float (*f)(float);
};

static struct d1 d1s[] = {
    { "cbrt", am_cbrt }, { "log1p", am_log1p }, { "expm1", am_expm1 }, { "log2", am_log2 }, { "exp2", am_exp2 },
    { "asinh", am_asinh }, { "acosh", am_acosh }, { "atanh", am_atanh }, { "erf", am_erf }, { "erfc", am_erfc },
    { "lgamma", am_lgamma }, { "tgamma", am_tgamma }
};

static struct f1 f1s[] = {
    { "sinf", am_sinf }, { "cosf", am_cosf }, { "tanf", am_tanf }, { "asinf", am_asinf }, { "acosf", am_acosf },
    { "atanf", am_atanf }, { "expf", am_expf }, { "logf", am_logf }, { "log10f", am_log10f },
    { "sqrtf", am_sqrtf }, { "sinhf", am_sinhf }, { "coshf", am_coshf }, { "tanhf", am_tanhf },
    { "cbrtf", am_cbrtf }, { "exp2f", am_exp2f }, { "log2f", am_log2f }, { "erff", am_erff },
    { "erfcf", am_erfcf }, { "lgammaf", am_lgammaf }, { "tgammaf", am_tgammaf }, { "log1pf", am_log1pf },
    { "expm1f", am_expm1f }, { "asinhf", am_asinhf }, { "acoshf", am_acoshf }, { "atanhf", am_atanhf }
};

static double from(unsigned long long u)
{
    double x;

    memcpy(&x, &u, 8);
    return x;
}

static void put_d(double r)
{
    uint64_t u;

    memcpy(&u, &r, 8);
    printf("%016llx\n", (unsigned long long)u);
}

static void put_f(float r)
{
    uint32_t u;

    memcpy(&u, &r, 4);
    printf("%08lx\n", (unsigned long)u);
}

int main(void)
{
    char line[200];
    char name[32];
    unsigned long long a;
    unsigned long long b;
    int n;
    int k;

    while (fgets(line, sizeof line, stdin) != NULL) {
        n = sscanf(line, "%31s %llx %llx", name, &a, &b);
        if (n < 2)
            continue;
        if (strcmp(name, "hypot") == 0) {
            put_d(am_hypot(from(a), from(b)));
        } else if (strcmp(name, "powf") == 0) {
            put_f(am_powf((float)from(a), (float)from(b)));
        } else if (strcmp(name, "atan2f") == 0) {
            put_f(am_atan2f((float)from(a), (float)from(b)));
        } else if (strcmp(name, "hypotf") == 0) {
            put_f(am_hypotf((float)from(a), (float)from(b)));
        } else {
            for (k = 0; k < (int)(sizeof d1s / sizeof d1s[0]); k++)
                if (strcmp(name, d1s[k].name) == 0)
                    break;
            if (k < (int)(sizeof d1s / sizeof d1s[0])) {
                put_d(d1s[k].f(from(a)));
                continue;
            }
            for (k = 0; k < (int)(sizeof f1s / sizeof f1s[0]); k++)
                if (strcmp(name, f1s[k].name) == 0)
                    break;
            if (k < (int)(sizeof f1s / sizeof f1s[0]))
                put_f(f1s[k].f((float)from(a)));
            else
                printf("unknown\n");
        }
        fflush(stdout);
    }
    return 0;
}
