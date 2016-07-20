#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>

#include <string.h>
#include <math.h>
#include <time.h>

#include <unistd.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <pwd.h>

#include <sensors/sensors.h>

#include "cbar.h"


/*
  must return an int, where said value is the amnount of
  milliseconds to wait before executing the same function
  again.
 */

bool info = false;
static const char* usr;
static char host[128];
struct utsname unamest;

rgba_t red, blue, green, yellow,
    dim, purple, white, back, eback;

static char timestr[128];
static time_t ti;
static struct tm *tm;
static struct sysinfo meminfo;
static long long totalmem, totalswap, usedmem, usedswap;

static void setup(void) {
    uid_t uid = getuid();
    struct passwd* pw = getpwuid(uid);
    if (pw) {
        usr = pw->pw_name;
    } else usr = "?";
    gethostname(host, 128);
    
    uname(&unamest);
    
    FILE* lm_conf = fopen("/etc/sensors.conf", "r");
    sensors_init(lm_conf);
    
    red    = bar_mkcolor("#cd5c5c");
    blue   = bar_mkcolor("#00bfff");
    green  = bar_mkcolor("#3cb371");
    yellow = bar_mkcolor("#ffa500");
    dim    = bar_mkcolor("#626262");
    purple = bar_mkcolor("#8968cd");
    white  = bar_mkcolor("#bfbfbf");
    back   = bar_mkcolor(CFG_BGCOLOR);
    eback  = bar_mkcolor("#7C7C7C");
    
    bar_lncolor('b', BAR_FG, blue);
    bar_lncolor('p', BAR_FG, purple);
    bar_lncolor('w', BAR_FG, white);
    bar_lncolor('E', BAR_BG, eback);
    bar_lncolor('e', BAR_U, back);
    bar_lncolor('!', BAR_BG, back);
    bar_lncolor('g', BAR_FG, green);
    bar_lncolor('y', BAR_FG, yellow);
    bar_lncolor('d', BAR_FG, dim);
    bar_lncolor('r', BAR_FG, red);
    
    sysinfo(&meminfo);
    totalmem = meminfo.totalram * meminfo.mem_unit;
    totalswap = meminfo.totalswap * meminfo.mem_unit;
    
    printf("SENSOR INFORMATION (use this to specify sensors)");
    
    sensors_chip_name const * cn;
    int c = 0;
    while ((cn = sensors_get_detected_chips(0, &c)) != 0) {
        printf("chip: %s:%s bus={.type=%d,.nr=%d} addr=%d\n",
               cn->prefix, cn->path, cn->bus.type, cn->bus.nr, cn->addr);

        sensors_feature const *feat;
        int f = 0;

        while ((feat = sensors_get_features(cn, &f)) != 0) {
            printf("%d: %s\n", f, feat->name);

            sensors_subfeature const *subf;
            int s = 0;

            while ((subf = sensors_get_all_subfeatures(cn, feat, &s)) != 0) {
                printf("%d:%d:%s/%d = ", f, s, subf->name, subf->number);
                double val;
                if (subf->flags & SENSORS_MODE_R) {
                    int rc = sensors_get_value(cn, subf->number, &val);
                    if (rc < 0) {
                        printf("err: %d", rc);
                    } else {
                        printf("%f", val);
                    }
                }
                puts("\n");
            }
        }
    }
}

struct cpustat {
    long double values[4];
};
static bool getcpuload(int core, struct cpustat* st) {
    FILE* f = fopen("/proc/stat", "r");
    char buf[1024];
    char set[1024];
    size_t r, i, idx = 0, s = 0;
    bool skip = false, ret = false;
    while (!feof(f)) {
        r = fread(buf, sizeof(char), sizeof(buf) / sizeof(char), f);
        for (i = 0; i < r; ++i) {
            if (skip) {
                if (buf[i] == '\n') {
                    skip = false;
                    idx = 0;
                    s = 0;
                }
            } else {
                switch (idx) {
                case 0 ... 2:
                    /* check if line starts with 'cpu' */
                    if (("cpu")[idx] != buf[i]) {
                        fprintf(stderr, "failed to find line in /proc/stat starting with \"cpu\""
                                " (expected: %c, got: %c)\n", ("cpu")[idx], buf[i]);
                        goto cleanup;
                    }
                    break;
                case 3:
                    /* check for core number */
                    if ('0' + (char) core != buf[i]) {
                        skip = true;
                        continue;
                    }
                    break;
                case 4: break; /* skip first space */
                default:
                    if (buf[i] != '\n') {
                        set[s] = buf[i];
                        ++s;
                    } else {
                        set[s] = '\0';
                        sscanf(set, "%Lf %Lf %Lf %Lf",
                               &st->values[0], &st->values[1],
                               &st->values[2], &st->values[3]);
                        ret = true;
                        goto cleanup;
                    }
                    break;
                }
                ++idx;
            }
        }
    }
 cleanup:
    if (f != NULL)
        fclose(f);
    return ret;
}

static long long getmeminfo(const char* key) {
    FILE* f = fopen("/proc/meminfo", "r");
    char buf[1024];
    char t_buf[1024];
    size_t i, ti = 0, key_sz = strlen(key), r;
    char last = '\0';
    bool match = false, start_num_token = false;
    long long ret = 0;
    while (!feof(f)) {
        r = fread(buf, sizeof(char), sizeof(buf) / sizeof(char), f);
        for (i = 0; i < r; ++i) {
            if (!match) {
                /* check for match at end of token */
                if (buf[i] == ':') {
                    
                    if (key_sz == ti && !strncmp(t_buf, key, ti)) {
                        match = true;
                        ti = 0;
                        continue;
                    }
                }
                if (last == '\n') { /* reset token */
                    ti = 0;
                }
                /* copy to token buf */
                t_buf[ti] = buf[i];
                last = buf[i];
                ++ti;
            } else {
                switch (buf[i]) {
                case ' ':
                    if (!start_num_token)
                        continue;
                    else {
                        t_buf[ti] = '\0';
                        ret = (long long) atoll(t_buf) * 1024;
                        goto ret;
                    }
                case '0' ... '9':
                    start_num_token = true;
                    t_buf[ti] = buf[i];
                    ++ti;
                }
            }
        }
    }
 ret:
    if (f != NULL)
        fclose(f);
    return ret;
}

static inline double memdivisor(long long v) {
    if (v > 1024 * 1024 * 1024) { // GB
        return 1024 * 1024 * 1024;
    } else if (v > 1024 * 1024) { // MB
        return 1024 * 1024;
    } else { // KB
        return 1024;
    }
}

static inline char memchar(long long v) {
    if (v > 1024 * 1024 * 1024) {
        return 'G';
    } else if (v > 1024 * 1024) {
        return 'M';
    } else {
        return 'K';
    }
}

static inline int lmvalue(sensors_chip_name* cn, int sf) {
    double d;
    if (sensors_get_value(cn, sf, &d) < 0) {
        fprintf(stderr, "error: sensors_get_value(...)");
        return -1;
    }
    return (int) round(d);
}

/* manually specify chip information */
static sensors_chip_name cpu_chip = {
    .bus    = { .type = 1, .nr = 0 },
    .addr   = 0,
    .prefix = "coretemp",
    .path   = "/sys/class/hwmon/hwmon2"
};

/* cpu colors */
#define CPU_STR(i) (i >= 70 ? "r" : (i >= 30 ? "y" : "g"))
/* temp colors */
#define TEMP_STR(i) (i >= 70 ? "r" : (i >= 50 ? "y" : "b"))
/* memory colors */
#define MEM_STR(i, m) (i >= m / (double) 1.4 ? "r" : (i >= m / 2 ? "y" : "g"))

static bool last_stat_set = false;;
static struct cpustat last_stat[8] = { [0 ... 7] = { .values = { [0 ... 3] = 0 } } };

RENDER_FUNC {
    if (!info) {
        setup();
        info = true;
    }
    ti = time(NULL);
    tm = localtime(&ti);
    strftime(timestr, sizeof(timestr), " #g%A#w, %B %e #b%l#w:#b%M#w:#b%S ", tm);

    sysinfo(&meminfo);
    /*
      it appears MemAvailable in recent kernels is (Free + Cached + Buffers + ...), so let's use that.
      this actually gives a different value than (h)top, and the kernel's documentation claims this is
      a decent estimate of memory available for starting new applications (and is system-dependant).
    */
    usedmem = (meminfo.totalram - (getmeminfo("MemAvailable"))) * meminfo.mem_unit;
    usedswap = (meminfo.totalswap - (meminfo.freeswap + getmeminfo("SwapCached"))) * meminfo.mem_unit;

    struct cpustat stat[8];
    int loadavg[8];
    size_t t;
    for (t = 0; t < 8; ++t) {
        if (!getcpuload(t, &stat[t])) {
            fprintf(stderr, "failed to get CPU load for core: %d\n", (int) t);
            exit(EXIT_FAILURE);
        }
        if (last_stat_set) {
            
            #define a (last_stat[t].values)
            #define b (stat[t].values)
            
            loadavg[t] = (int) round((long double)
                (((b[0] + b[1] + b[2]) - (a[0] + a[1] + a[2]))
                 / ((b[0] + b[1] + b[2] + b[3]) - (a[0] + a[1] + a[2] + a[3]))) * 100);
            if (loadavg[t] >= 100)
                loadavg[t] = 99;
            
            #undef a
            #undef b
            
        } else {
            loadavg[t] = 0;
        }
    }
    
    bar_offset(60);
    bar_printf("(#b%s#w@#p%s#w) %s #y%s#w", usr, host,
               unamest.sysname, unamest.release);
    bar_offset(80);
    bar_printf("#dmem: #%s%.2f%c#w/#w%.2f%c  #dswap: #%s%.2f%c#w/#w%.2f%c  #dprocs: #w%d",
               MEM_STR(usedmem, totalmem), (double) (usedmem / memdivisor(usedmem)), memchar(usedmem), 
               (double) (totalmem / memdivisor(totalmem)), memchar(totalmem), 
               MEM_STR(usedswap, totalswap), (double) (usedswap / memdivisor(usedswap)), memchar(usedswap), 
               (double) (totalswap / memdivisor(totalswap)), memchar(totalswap),
               (int) meminfo.procs);
    bar_centeralign();
    bar_colorsegment(timestr);
    bar_rightalign();
    int sd = lmvalue(&cpu_chip, 0),
        s0 = lmvalue(&cpu_chip, 4),
        s1 = lmvalue(&cpu_chip, 8),
        s2 = lmvalue(&cpu_chip, 12),
        s3 = lmvalue(&cpu_chip, 16);
    
    bar_colorsegment("#dCPU [0..7]:#w { #d");
    int n;
    for (n = 0; n < 8; ++n)
        bar_printf(n == 7 ? "#%s%02d#w" : "#%s%02d#d|", CPU_STR(loadavg[n]), loadavg[n]);
    bar_puts(" }");
    bar_offset(90);
    
    bar_printf("#dtemp [0..3]:#w { #dphys:#%s%d#d, #%s%d#d|#%s%d#d|#%s%d#d|#%s%d#w }",
               TEMP_STR(sd), sd,
               TEMP_STR(s0), s0,
               TEMP_STR(s1), s1,
               TEMP_STR(s2), s2,
               TEMP_STR(s3), s3);
    bar_offset(70);

    memcpy(&last_stat, &stat, sizeof(stat));
    last_stat_set = true;
    
    return 1000;
}
