/*
 * sdktest.c - tries the MCP's Calypsi SDK on the machine
 *
 * Run it from the MCP command line (sdktest.pgz). It calls the system calls whose layout or name
 * changed against Calypsi's own SDK and prints what came back, so it can be checked by eye:
 * - sys_get_info: the 2.1.0 record (sub_model, CPU and bus clocks)
 * - sys_fsys_get_cwd and its older name sys_fsys_getcwd: both give the current folder
 * - sys_fsys_stat: size and date of the program's own file
 * - sys_time_getrtc, sys_txt_get_sizes, sys_err_message
 * - sys_txt_topbar: text in the top bar until the program ends
 * - stdio (printf, fopen): Calypsi's library calls the MCP through the same functions
 */

#include <stdio.h>
#include <string.h>
#include <mcp/syscalls.h>

/*
 * Print one line "name: result" for a call that returns an MCP result code.
 *
 * Input:  name = what was called; result = its result (0 or more is fine, negative an error)
 * Output: none
 */
static void show_result(const char * name, short result) {
    if (result < 0) {
        printf("%-22s error %d: %s\n", name, result, sys_err_message(result));
    } else {
        printf("%-22s %d\n", name, result);
    }
}

/*
 * The test: each block calls one part of the SDK and prints what it got.
 *
 * Input:  argc, argv = the command line; argv[0] is the program's name as typed, used for the stat test
 * Output: 0
 */
int main(int argc, char * argv[]) {
    t_sys_info info;
    t_file_info file;
    t_time time;
    t_extent text_size, pixel_size;
    char cwd[MAX_PATH_LEN];
    char old_cwd[MAX_PATH_LEN];
    char prog[MAX_PATH_LEN];
    short result;
    const char * path = (argc > 0) ? argv[0] : "sdktest.pgz";
    FILE * f;
    char line[40];

    printf("MCP Calypsi SDK test (SDK %d.%d.%d)\n\n",
           MCP_SDK_VERSION_MAJOR, MCP_SDK_VERSION_MINOR, MCP_SDK_VERSION_BUILD);

    /* The record whose layout changed: every field after model moved */
    memset(&info, 0, sizeof(info));
    sys_get_info(&info);
    printf("MCP %d.%d.%d on %s (model %d, sub-model %d)\n",
           info.mcp_version, info.mcp_rev, info.mcp_build, info.model_name, info.model, info.sub_model);
    printf("CPU %s (%d) at %lu kHz, bus %lu kHz\n",
           info.cpu_name, info.cpu, (unsigned long)info.cpu_clock_khz, (unsigned long)info.bus_clock_khz);
    printf("FPGA %08lX model %08lX version %04X.%04X\n",
           (unsigned long)info.fpga_date, (unsigned long)info.fpga_model, info.fpga_version, info.fpga_subver);
    printf("RAM %lu bytes, %d screens, ethernet %s\n\n",
           (unsigned long)info.system_ram_size, info.screens, info.has_ethernet ? "yes" : "no");

    /* The current folder, under the 2.1.0 name and the older one */
    show_result("sys_fsys_get_cwd", sys_fsys_get_cwd(cwd, sizeof(cwd)));
    show_result("sys_fsys_getcwd", sys_fsys_getcwd(old_cwd, sizeof(old_cwd)));
    printf("  \"%s\" / \"%s\" %s\n\n", cwd, old_cwd, strcmp(cwd, old_cwd) == 0 ? "(same)" : "(DIFFERENT)");

    /* sys_fsys_stat, new to the SDK. argv[0] is the name as typed on the command line ("SDKTEST"):
     * the MCP found the file by adding ".PGZ", so do the same when the name has no extension. */
    strncpy(prog, path, sizeof(prog) - 5);
    prog[sizeof(prog) - 5] = 0;
    result = sys_fsys_stat(prog, &file);
    if (result < 0 && strchr(prog, '.') == 0) {
        strcat(prog, ".PGZ");
        result = sys_fsys_stat(prog, &file);
    }
    show_result("sys_fsys_stat", result);
    if (result >= 0) {
        /* Only on success: after an error the record holds nothing */
        printf("  %s: %ld bytes, %04d-%02d-%02d %02d:%02d, attributes %02X\n",
               prog, file.size,
               1980 + (file.date >> 9), (file.date >> 5) & 0x0F, file.date & 0x1F,
               file.time >> 11, (file.time >> 5) & 0x3F, file.attributes);
    }
    show_result("sys_fsys_stat (none)", sys_fsys_stat("no_such_file.xyz", &file));
    printf("\n");

    /* The clock and the screen */
    sys_time_getrtc(&time);
    printf("RTC %04d-%02d-%02d %02d:%02d:%02d\n", time.year, time.month, time.day,
           time.hour, time.minute, time.second);
    sys_txt_get_sizes(0, &text_size, &pixel_size);
    printf("Screen 0: %d x %d characters, %d x %d pixels\n\n",
           text_size.width, text_size.height, pixel_size.width, pixel_size.height);

    /* The top bar, new in 2.1.0 */
    show_result("sys_txt_topbar", sys_txt_topbar(0, "SDK test"));

    /* Elevate is refused in 2.1.0 */
    show_result("sys_proc_elevate", sys_proc_elevate());

    /* stdio goes through the same system calls (Calypsi's stubs call sys_fsys_open, ...) */
    f = fopen("sdktest.txt", "w");
    if (f) {
        fputs("written by sdktest\n", f);
        fclose(f);
        f = fopen("sdktest.txt", "r");
        if (f && fgets(line, sizeof(line), f)) {
            printf("stdio file             %s", line);
        }
        if (f) {
            fclose(f);
        }
        show_result("sys_fsys_delete", sys_fsys_delete("sdktest.txt"));
    } else {
        printf("stdio file             fopen failed\n");
    }

    printf("\nPress a key (the top bar text goes when the program ends)\n");
    sys_chan_read_b(0);
    return 0;
}
