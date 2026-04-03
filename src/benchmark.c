#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

typedef struct {
    char name[20];
    double c_time;
    double d_time;
    size_t size;
    int success;
} Stats;

double get_t() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

size_t get_sz(const char *f) {
    struct stat st;
    if (stat(f, &st) != 0) return 0;
    return st.st_size;
}

Stats run_bench(const char *name, const char *c_cmd, const char *d_cmd, const char *c_out) {
    Stats s = {0};
    strncpy(s.name, name, 19);
    double start, end;

    start = get_t();
    if (system(c_cmd) != 0) return s;
    end = get_t();
    s.c_time = end - start;
    s.size = get_sz(c_out);

    start = get_t();
    if (system(d_cmd) != 0) return s;
    end = get_t();
    s.d_time = end - start;
    
    s.success = 1;
    return s;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <input_file>\n", argv[0]);
        return 1;
    }

    const char *in = argv[1];
    size_t orig_sz = get_sz(in);
    if (!orig_sz) return 1;

    char cmd_c[512], cmd_d[512];
    Stats r[4];

    sprintf(cmd_c, "./compress %s tmp.custom", in);
    sprintf(cmd_d, "./decompress tmp.custom ver.custom");
    r[0] = run_bench("RCF", cmd_c, cmd_d, "tmp.custom");

    sprintf(cmd_c, "gzip -9 -c %s > tmp.gz", in);
    sprintf(cmd_d, "gzip -d -c tmp.gz > ver.gz");
    r[1] = run_bench("Gzip -9", cmd_c, cmd_d, "tmp.gz");

    sprintf(cmd_c, "bzip2 -9 -c %s > tmp.bz2", in);
    sprintf(cmd_d, "bzip2 -d -c tmp.bz2 > ver.bz2");
    r[2] = run_bench("Bzip2 -9", cmd_c, cmd_d, "tmp.bz2");

    sprintf(cmd_c, "xz -9 -c %s > tmp.xz", in);
    sprintf(cmd_d, "xz -d -c tmp.xz > ver.xz");
    r[3] = run_bench("LZMA/XZ -9", cmd_c, cmd_d, "tmp.xz");

    printf("\n%-12s | %-10s | %-8s | %-8s | %-8s\n", "Algorithm", "Size", "Ratio", "Comp", "Decomp");
    printf("----------------------------------------------------------------\n");
    
    for (int i = 0; i < 4; i++) {
        if (!r[i].success) {
            printf("%-12s | FAILED\n", r[i].name);
            continue;
        }
        double ratio = (1.0 - (double)r[i].size / orig_sz) * 100.0;
        printf("%-12s | %-10zu | %6.2f%%  | %7.4fs | %7.4fs\n", 
               r[i].name, r[i].size, ratio, r[i].c_time, r[i].d_time);
    }

    system("rm -f tmp.* ver.*");
    return 0;
}
