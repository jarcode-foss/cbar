#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>

#include <string.h>
#include <math.h>
#include <time.h>
#include <errno.h>

/* linux/posix specific */
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <pwd.h>
#include <pthread.h>

/* libsensors */
#include <sensors/sensors.h>

/* libalpm - pacman library thingy */
#include <alpm.h>
#include <alpm_list.h>

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

static alpm_errno_t pk_err;
static alpm_db_t* ldb;                  /* local db */
static alpm_handle_t* pk_handle;        /* lib handle for /var/lib/pacman */
static alpm_handle_t* pk_lhandle;       /* lib handle for dummy db */
static bool alpm_check = true;

#define ALPM_DUMMY "cbar_alpm_dummydb"
#define ALPM_DUMMY_PATH "/tmp/" ALPM_DUMMY

#define NEW(T, ...) ({ T* self = malloc(sizeof(T)); *self = (T) __VA_ARGS__; self; })
#define FN(R, F) ({ R _f F; _f; })
#define ATOMIC_ACCESS(V) \
    ({ __sync_synchronize(); __auto_type _v = (V); __sync_synchronize(); _v; })
#define ATOMIC_SET(A, V) \
    ({ __sync_synchronize(); __auto_type _v = (V); A = _v; __sync_synchronize(); _v; })
/* compare and swap, the builtins are really long */
#define CASB(P, O, N) __sync_bool_compare_and_swap(P, O, N)
#define CAS(P, O, N) __sync_val_compare_and_swap(P, O, N)

#define ALPM_COMPLAIN(H, S)                                       \
    do {                                                          \
        fprintf(stderr, "ALPM error (" S ") at %s:%d (%d): %s\n", \
                __func__, __LINE__, alpm_errno(H),                \
                alpm_strerror(alpm_errno(H)));                    \
        alpm_check = false;                                       \
    } while (0)

#if __x86_64__
#define ARCH_STR "x86_64"
#elif __i686__
#define ARCH_STR "i686"
#else
#error "arch"
#endif

static struct pk_status {
    alpm_db_t*        alpm_db;
    const char const* db;
    const char const* name;
    const char*       local_version;
    const char*       remote_version;
    const char const* dname;
    bool              ignore;
} pk_checks[] = {
    /* really important packages */
    { .db = "core", .name = "linux" },
    { .db = "core", .name = "linux-firmware" },
    { .db = "core", .name = "glibc" },
    { .db = "multilib", .name = "gcc-multilib", .dname = "gcc" },
    { .db = "core", .name = "systemd" },
    { .db = "extra", .name = "emacs" },
    { .db = "core", .name = "bash" },
    
    /* security libraries, we include 32-bit versions because sometimes there are 32-bit specific updates */
    { .db = "core", .name = "openssl" }, { .db = "multilib", .name = "lib32-openssl", .dname = "openssl" },
    { .db = "core", .name = "libgcrypt" }, { .db = "multilib", .name = "lib32-libgcrypt", .dname = "libgcrypt" },
    { .db = "core", .name = "libsasl " }, { .db = "multilib", .name = "lib32-libsasl", .dname = "libsasl" },
    { .db = "core", .name = "gnutls" }, { .db = "multilib", .name = "lib32-gnutls", .dname = "gnutls" },

    /* other important packages */
    { .db = "core", .name = "netctl" }, /* core system tools */
    { .db = "core", .name = "dbus" },
    { .db = "core", .name = "openssh" },
    
    { .db = "extra", .name = "nvidia" }, /* graphics */
    { .db = "extra", .name = "libglvnd" },
    
    { .db = "extra", .name = "firefox" }, /* browser and certs */
    { .db = "core", .name = "ca-certificates" },
    { .db = "core", .name = "ca-certificates-mozilla" },
    
    { .db = "extra", .name = "gtk3" },
};

static void pk_update_local(void) {
    /* iterate packages to check, and store local versions */
    
    ldb = alpm_get_localdb(pk_lhandle); /* doesn't error ? */
    
    {
        size_t n;
        for (n = 0; n < (sizeof(pk_checks) / sizeof(struct pk_status)); ++n) {
            struct pk_status* s = pk_checks + n;
            alpm_pkg_t* pkg;
            if (s->ignore)
                continue;
            if (!(pkg = alpm_db_get_pkg(ldb, s->name))) {
                fprintf(stderr, "failed to find %s/%s in local database\n",
                        s->db, s->name);
                ALPM_COMPLAIN(pk_handle, "alpm_db_get_pkg");
                s->ignore = true;
                continue;
            }
            if (!(s->local_version = alpm_pkg_get_version(pkg))) {
                fprintf(stderr, "failed to get version for %s/%s in local database\n",
                        s->db, s->name);
                ALPM_COMPLAIN(pk_handle, "alpm_pkg_get_version");
                s->ignore = true;
                continue;
            }
            s->ignore = false;
        }
    }
}

static const char* pk_version(alpm_db_t* db, const char* package,
                              const char* lver, alpm_list_t* dcache) {
    alpm_pkg_t* p;
    bool u = true;
    switch (dcache ? ((u = (alpm_list_find_ptr(dcache, db) != NULL)) ? 2 : alpm_db_update(0, db)) : 2) {
    case -1:
        ALPM_COMPLAIN(pk_handle, "alpm_db_update");
        return NULL;
    case 2: /* database already updated in cache, or ignore update */
        if (dcache) printf("^ ");
        goto grab;
    case 0: /* db just updated */
        if (dcache) printf("* ");
        goto grab;
    case 1: /* db is already up to date */
        if (dcache) printf("+ ");
    grab:
        if (!u)
            alpm_list_add(dcache, db);
        p = alpm_db_get_pkg(db, package);
        if (!p) {
            fprintf(stderr, "no package '%s' in specified database!\n", package);
            ALPM_COMPLAIN(pk_handle, "alpm_db_get_pkg");
            return NULL;
        }
        const char* n = alpm_pkg_get_version(p);
        if (dcache) printf("%s: '%s', '%s'\n", package, n, lver);
        if (alpm_pkg_vercmp(n, lver)) {
            return n;
        } else return NULL;
    }
    return NULL;
}

static void pk_update(bool remote, void (*fn) (const char*, const char*)) {

    if (remote)
        printf("checking for updates (%d)...\n", (int) (sizeof(pk_checks) / sizeof(struct pk_status)));

    pk_update_local();
    
    alpm_list_t* cache = remote ? NEW(alpm_list_t, { .prev = self, .data = NULL, .next = NULL }) : NULL;
    alpm_list_t* pcache = NEW(alpm_list_t, { .prev = self, .data = NULL, .next = NULL });
    size_t n;
    for (n = 0; n < (sizeof(pk_checks) / sizeof(struct pk_status)); ++n) {
        struct pk_status* s = pk_checks + n;
        if (!(s->ignore) && (s->remote_version =
                             pk_version(s->alpm_db, s->name, s->local_version, remote ? cache : NULL))) {
            const char* pkg = s->dname ? s->dname : s->name;
            if (!alpm_list_find_str(pcache, pkg)) {
                fn(s->dname ? s->dname : s->name, s->remote_version);
                alpm_list_add(pcache, (void*) pkg);
            }
        }
    }
    if (cache)
        alpm_list_free(cache);
    alpm_list_free(pcache);
}

/*
  generate the (string) list of packages that have updates, 'lock' is a primitive atomic lock.
  this function returns false if it is already executing under the same lock.
*/
static bool pk_async_strgen(bool _remote, pthread_t* storage, char* _buf,
                            size_t _s, volatile bool* _lock) {
    
    if (!CASB(_lock, false, true)) {
        return false;
    }

    /* static because of storage duration for inline function */
    static bool remote;
    static char* buf;
    static size_t s;
    static volatile bool* lock;

    remote = _remote;
    buf = _buf;
    s = _s;
    lock = _lock;
    
    void* entry(void* p) {
        size_t t = 0, a = 0;
        pk_update(remote, FN(void, (const char* pkg, const char* ver) {
                    t += snprintf(buf + t, s - t, t == 0 ? "%s" : ", %s", pkg);
                    ++a;
                }));
        if (t == 0) {
            strncpy(buf, "#dno important updates", s);
        }
        ATOMIC_SET(*lock, false);
        return NULL;
    }
    pthread_create(storage, NULL, &entry, NULL);
    return true;
}

bool parse_mirrorlist(const char* repo, const char* arch, void (*fn) (const char* s)) {
    
    inline void fcl(FILE** _f) { fclose(*_f); }
    FILE* f __attribute__((__cleanup__(fcl)))
        = fopen("/etc/pacman.d/mirrorlist", "r");
    
    if (!f) {
        fprintf(stderr, "error opening mirrorlist: %s\n", strerror(ferror(f)));
        return false;
    }
    int r,     /* input char */
        i = 0; /* line idx */
    char line[1024];
    while ((r = fgetc(f)) != EOF) {
        char c = (char) r;
        switch (c) {
        case '\n':
            if (!strncmp(line, "Server = ", 9)) {
                char buf[1536];
                char* s,            /* char position */
                    * l = line + 9; /* start of segment */
                size_t t = 0;       /* index in buf */
                for (s = line + 9; s - line < i; ++s) {
                    if (*s == '$') {
                        int k;
                        if ((k = !strncmp(s + 1, "repo", 4) ?
                             1 : (!strncmp(s + 1, "arch", 4) ? 2 : 0))) {
                            memcpy(buf + t, l, s - l);
                            t += s - l;
                            strcpy(buf + t, k == 1 ? repo : arch);
                            l = s + 5;
                            t += strlen(k == 1 ? repo : arch);
                        }
                    }
                }
                if (l < s) {
                    memcpy(buf + t, l, s - l); /* copy last segment */
                    t += s - l;
                }
                if (t != 0) {
                    buf[t] = '\0';
                    fn(buf);
                }
            }
            i = 0;
            break;
        default:
            if (i != sizeof(line)) {
                line[i] = c;
                ++i;
            } else {
                fprintf(stderr, "error reading mirrorlist: line too long (%lu)\n", sizeof(line));
                return false;
            }
            break;
        }
    }
    return true;
}

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
    
    alpm_list_t* db_list; /* all sync dbs */
 
    errno = 0;
    if (mkdir(ALPM_DUMMY_PATH, 0777) && errno != EEXIST) {
        fprintf(stderr, "mkdir(\"%s\", ...) failed: %s", ALPM_DUMMY_PATH, strerror(errno));
        goto after_alpm;
    }
    
    /* ALPM */
    
    pk_handle = alpm_initialize("/", ALPM_DUMMY_PATH, &pk_err);
    if (!pk_handle) {
        ALPM_COMPLAIN(pk_handle, "alpm_initialize - dummy");
        goto after_alpm;
    }
    
    pk_lhandle = alpm_initialize("/", "/var/lib/pacman", &pk_err);
    if (!pk_lhandle) {
        ALPM_COMPLAIN(pk_handle, "alpm_initialize - local");
        goto after_alpm;
    }

    alpm_option_set_logcb(pk_handle, FN(void, (alpm_loglevel_t level, const char* msg, va_list args) {
                // printf("[ALPM:LOG] ");
                // vprintf(msg, args);
            }));

    /* dummy dbs in /tmp */

    /* function to register db */
    alpm_db_t* reg_db(const char* name) {
        alpm_db_t* db = alpm_register_syncdb(pk_handle, name,
                                             ALPM_SIG_PACKAGE | ALPM_SIG_PACKAGE_OPTIONAL |
                                             ALPM_SIG_DATABASE | ALPM_SIG_DATABASE_OPTIONAL);
        if (!db) {
            fprintf(stderr, "failed to register \"%s\" db\n", name);
            ALPM_COMPLAIN(pk_handle, "alpm_register_syncdb");
            return NULL;
        }
    
        parse_mirrorlist(name, ARCH_STR, FN(void, (const char* m) {
                    if (alpm_db_add_server(db, m) != 0) {
                        ALPM_COMPLAIN(pk_handle, "alpm_db_add_server");
                        printf("failed to register mirror: '%s'\n", m);
                    }
                }));
        return db;
    }

    /* iterate packages to check and register dbs as required */
    {
        size_t n;
        for (n = 0; n < (sizeof(pk_checks) / sizeof(struct pk_status)); ++n) {
            db_list = alpm_get_syncdbs(pk_handle);
            struct pk_status* s = pk_checks + n;
            alpm_db_t* r;
            if (!(r = alpm_list_find(db_list, NULL, FN(int, (const void* data, const void* ignored) {
                                return strcmp(alpm_db_get_name(data), s->db); })))) {
                if (!(s->alpm_db = reg_db(s->db))) {
                    s->ignore = true;
                } else printf("registered '%s' database for '%s'\n", s->db, s->name);
            }
            else {
                s->alpm_db = r;
                printf("using loaded database for '%s/%s'\n", s->db, s->name);
            }
        }
    }

    /* local, real db */

    pk_update_local();
    
 after_alpm:

    /* COLORS */
    
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
static pthread_t update_thread;
static volatile bool update_alock = false;
static char update_strbuf[64];

#define UPDATE_INTVAL (60 * 5)

static int ucounter = UPDATE_INTVAL - 5, acounter = 0, lucounter = 0;

RENDER_FUNC {
    if (!info) {
        setup();
        info = true;
    }
    ti = time(NULL);
    tm = localtime(&ti);
    strftime(timestr, sizeof(timestr), " #g%A#w, %B %e #b%l#w:#b%M#w:#b%S ", tm);

    sysinfo(&meminfo);

    if (!ucounter) {
        if (pk_async_strgen(true, &update_thread, update_strbuf, sizeof(update_strbuf), &update_alock)) {
            ++ucounter; /* only increment if the update was launched (not locked) */
        }
    } else ucounter++;
    
    if (!lucounter) {
        pk_async_strgen(false, &update_thread, update_strbuf, sizeof(update_strbuf), &update_alock);
    }
    
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
    
    bar_offset(40);
    bar_printf("#dmem: #%s%.2f%c#w/#w%.2f%c  #dswap: #%s%.2f%c#w/#w%.2f%c  #dprocs: #w%d",
               MEM_STR(usedmem, totalmem), (double) (usedmem / memdivisor(usedmem)), memchar(usedmem), 
               (double) (totalmem / memdivisor(totalmem)), memchar(totalmem), 
               MEM_STR(usedswap, totalswap), (double) (usedswap / memdivisor(usedswap)), memchar(usedswap), 
               (double) (totalswap / memdivisor(totalswap)), memchar(totalswap),
               (int) meminfo.procs);
    bar_offset(40);
    bar_printf("!: #y%s#w", ATOMIC_ACCESS(update_alock) || *update_strbuf == '\0' ?
               (acounter == 0 ? ".  " : (acounter == 1 ? ".. " : "...")) : /* ... animation while updating */
               update_strbuf);
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
    bar_offset(40);
    
    bar_printf("#dtemp [0..3]:#w { #dphys:#%s%d#d, #%s%d#d|#%s%d#d|#%s%d#d|#%s%d#w }",
               TEMP_STR(sd), sd,
               TEMP_STR(s0), s0,
               TEMP_STR(s1), s1,
               TEMP_STR(s2), s2,
               TEMP_STR(s3), s3);
    bar_offset(40);

    memcpy(&last_stat, &stat, sizeof(stat));
    last_stat_set = true;
    
    /* update check counter */
    ++acounter;
    if (ucounter >= UPDATE_INTVAL) {
        ucounter = 0;
    }
    
    if (acounter >= 3) {
        acounter = 0;
    }

    lucounter++;
    if (lucounter >= 5) {
        lucounter = 0;
    }
    
    return 1000;
}
