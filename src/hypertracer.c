// QEMU Hypertracer Plugin - traces hypercalls from guest to host kernel

#include "../include/hypertracer.h"

tracer_data_t *g_tracer = NULL;
static qemu_plugin_id_t plugin_id;

/* Initialize the plugin */
QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, 
                                          const qemu_info_t *info,
                                          int argc, char **argv)
{
    plugin_id = id;
    
    g_tracer = g_malloc0(sizeof(tracer_data_t));
    if (!g_tracer) {
        fprintf(stderr, "Failed to allocate tracer data\n");
        return -1;
    }
    
    // Initialize hash tables
    g_tracer->processes = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                               NULL, g_free);
    g_tracer->cr3_to_pid = g_hash_table_new(g_direct_hash, g_direct_equal);
    
    // Allocate trace arrays
    g_tracer->hypercalls = g_malloc0(sizeof(hypercall_trace_t) * MAX_HYPERCALLS_PER_PROCESS * MAX_PROCESSES);
    g_tracer->syscalls = g_malloc0(sizeof(syscall_trace_t) * MAX_HYPERCALLS_PER_PROCESS * MAX_PROCESSES);
    
    if (!g_tracer->hypercalls || !g_tracer->syscalls) {
        fprintf(stderr, "Failed to allocate trace arrays\n");
        cleanup_data();
        return -1;
    }
    
    g_tracer->hypercall_index = 0;
    g_tracer->syscall_index = 0;
    g_tracer->trace_enabled = true;
    g_tracer->start_time = time(NULL);
    
    g_tracer->output_file = fopen("hypertracer_output.txt", "w");
    if (!g_tracer->output_file) {
        fprintf(stderr, "Failed to open output file\n");
        cleanup_data();
        return -1;
    }
    
    write_trace_header();
    
    // Register for instruction execution callbacks
    qemu_plugin_register_vcpu_tb_exec_cb(NULL, QEMU_PLUGIN_CB_NO_REGS,
                                        on_instruction_exec);
    
    printf("Hypertracer plugin loaded successfully\n");
    printf("Tracing hypercalls and syscalls...\n");
    
    return 0;
}

/* Plugin exit handler */
void qemu_plugin_exit(qemu_plugin_id_t id, void *p)
{
    printf("Hypertracer plugin exiting...\n");
    
    if (g_tracer) {
        write_process_summary();
        print_statistics();
        generate_report();
        cleanup_data();
    }
    
    printf("Hypertracer plugin unloaded\n");
}

process_info_t* get_or_create_process(uint64_t pid, uint64_t cr3)
{
    process_info_t *proc = g_hash_table_lookup(g_tracer->processes, GUINT_TO_POINTER(pid));
    
    if (!proc) {
        proc = g_malloc0(sizeof(process_info_t));
        proc->pid = pid;
        proc->cr3 = cr3;
        proc->first_seen = time(NULL);
        proc->active = true;
        snprintf(proc->name, MAX_PROCESS_NAME, "process_%lu", pid);
        
        g_hash_table_insert(g_tracer->processes, GUINT_TO_POINTER(pid), proc);
        g_hash_table_insert(g_tracer->cr3_to_pid, GUINT_TO_POINTER(cr3), GUINT_TO_POINTER(pid));
        
        fprintf(g_tracer->output_file, "NEW_PROCESS: PID=%lu CR3=0x%lx NAME=%s\n", 
                pid, cr3, proc->name);
        fflush(g_tracer->output_file);
    }
    
    proc->last_seen = time(NULL);
    return proc;
}

uint64_t get_current_pid(void)
{
    // TODO: Extract PID from guest task_struct 
    return g_tracer->current_pid;
}

uint64_t get_current_cr3(void)
{
    // TODO: Read CR3 register value
    return g_tracer->current_cr3;
}

bool is_hypercall_instruction(uint64_t pc, uint64_t insn)
{
    // Check for common hypercall instruction patterns
    // x86_64 VMCALL instruction
    if ((insn & 0xFFFFFF) == 0x01C10F) {
        return true;
    }
    
    // x86_64 VMMCALL instruction  
    if ((insn & 0xFFFFFF) == 0x01D90F) {
        return true;
    }
    
    return false;
}

bool is_syscall_instruction(uint64_t pc, uint64_t insn)
{
    // x86_64 SYSCALL
    if ((insn & 0xFFFF) == 0x050F) {
        return true;
    }
    
    // x86 INT 0x80  
    if ((insn & 0xFFFF) == 0x80CD) {
        return true;
    }
    
    return false;
}

uint64_t read_guest_register(unsigned int vcpu_index, const char *reg_name)
{
    // Placeholder - would use QEMU API to read guest registers
    return 0;
}

void on_instruction_exec(unsigned int vcpu_index, void *userdata)
{
    struct qemu_plugin_insn *insn = (struct qemu_plugin_insn *)userdata;
    uint64_t pc = qemu_plugin_insn_vaddr(insn);
    uint64_t insn_data = 0; // TODO: extract actual instruction bytes
    
    // Update current process context
    uint64_t cr3 = read_guest_register(vcpu_index, "cr3");
    if (cr3 != g_tracer->current_cr3) {
        g_tracer->current_cr3 = cr3;
        gpointer pid_ptr = g_hash_table_lookup(g_tracer->cr3_to_pid, GUINT_TO_POINTER(cr3));
        if (pid_ptr) {
            g_tracer->current_pid = GPOINTER_TO_UINT(pid_ptr);
        } else {
            // New process - assign incremental PID
            static uint64_t next_pid = 1;
            g_tracer->current_pid = next_pid++;
            get_or_create_process(g_tracer->current_pid, cr3);
        }
    }
    
    if (is_hypercall_instruction(pc, insn_data)) {
        trace_hypercall_entry(vcpu_index, userdata);
    }
    
    if (is_syscall_instruction(pc, insn_data)) {
        trace_syscall_entry(vcpu_index, userdata);
    }
}

void trace_hypercall_entry(unsigned int vcpu_index, void *userdata)
{
    if (!g_tracer->trace_enabled || g_tracer->hypercall_index >= MAX_HYPERCALLS_PER_PROCESS * MAX_PROCESSES) {
        return;
    }
    
    hypercall_trace_t *trace = &g_tracer->hypercalls[g_tracer->hypercall_index++];
    
    trace->timestamp = time(NULL) - g_tracer->start_time;
    trace->pid = g_tracer->current_pid;
    trace->pc = 0; // TODO: get from instruction info
    
    // Read hypercall arguments from registers (x86_64 calling convention)
    trace->hypercall_num = read_guest_register(vcpu_index, "rax");
    trace->arg1 = read_guest_register(vcpu_index, "rdi");
    trace->arg2 = read_guest_register(vcpu_index, "rsi");
    trace->arg3 = read_guest_register(vcpu_index, "rdx");
    trace->arg4 = read_guest_register(vcpu_index, "r10");
    trace->arg5 = read_guest_register(vcpu_index, "r8");
    
    trace->completed = false;
    
    process_info_t *proc = get_or_create_process(g_tracer->current_pid, g_tracer->current_cr3);
    if (proc) {
        proc->hypercall_count++;
    }
    
    fprintf(g_tracer->output_file, "HYPERCALL_ENTRY: PID=%lu NUM=%lu(%s) ARGS=[%lu,%lu,%lu,%lu,%lu] PC=0x%lx TIME=%lu\n",
            trace->pid, trace->hypercall_num, get_hypercall_name(trace->hypercall_num),
            trace->arg1, trace->arg2, trace->arg3, trace->arg4, trace->arg5,
            trace->pc, trace->timestamp);
    fflush(g_tracer->output_file);
}

void trace_syscall_entry(unsigned int vcpu_index, void *userdata)
{
    if (!g_tracer->trace_enabled || g_tracer->syscall_index >= MAX_HYPERCALLS_PER_PROCESS * MAX_PROCESSES) {
        return;
    }
    
    syscall_trace_t *trace = &g_tracer->syscalls[g_tracer->syscall_index++];
    
    trace->timestamp = time(NULL) - g_tracer->start_time;
    trace->pid = g_tracer->current_pid;
    trace->pc = 0; // TODO: get from instruction info
    
    // Read syscall arguments from registers
    trace->syscall_num = read_guest_register(vcpu_index, "rax");
    trace->args[0] = read_guest_register(vcpu_index, "rdi");
    trace->args[1] = read_guest_register(vcpu_index, "rsi");
    trace->args[2] = read_guest_register(vcpu_index, "rdx");
    trace->args[3] = read_guest_register(vcpu_index, "r10");
    trace->args[4] = read_guest_register(vcpu_index, "r8");
    trace->args[5] = read_guest_register(vcpu_index, "r9");
    
    trace->completed = false;
    
    process_info_t *proc = get_or_create_process(g_tracer->current_pid, g_tracer->current_cr3);
    if (proc) {
        proc->syscall_count++;
    }
    
    fprintf(g_tracer->output_file, "SYSCALL_ENTRY: PID=%lu NUM=%lu ARGS=[%lu,%lu,%lu,%lu,%lu,%lu] PC=0x%lx TIME=%lu\n",
            trace->pid, trace->syscall_num,
            trace->args[0], trace->args[1], trace->args[2], 
            trace->args[3], trace->args[4], trace->args[5],
            trace->pc, trace->timestamp);
    fflush(g_tracer->output_file);
}

/* Get hypercall name */
const char* get_hypercall_name(uint64_t hypercall_num)
{
    switch (hypercall_num) {
        case HYPERCALL_MEMORY_OP: return "memory_op";
        case HYPERCALL_SCHED_OP: return "sched_op";
        case HYPERCALL_CONSOLE_IO: return "console_io";
        case HYPERCALL_XEN_VERSION: return "xen_version";
        case HYPERCALL_EVENT_CHANNEL: return "event_channel_op";
        case HYPERCALL_PHYSDEV_OP: return "physdev_op";
        case HYPERCALL_GRANT_TABLE: return "grant_table_op";
        case HYPERCALL_VM_ASSIST: return "vm_assist";
        case HYPERCALL_UPDATE_VA_MAP: return "update_va_mapping";
        case HYPERCALL_HVM_OP: return "hvm_op";
        default: return "unknown";
    }
}

/* Write trace file header */
void write_trace_header(void)
{
    fprintf(g_tracer->output_file, "# QEMU Hypertracer Output\n");
    fprintf(g_tracer->output_file, "# Start Time: %s", ctime(&g_tracer->start_time));
    fprintf(g_tracer->output_file, "# Format: TYPE: FIELDS...\n");
    fprintf(g_tracer->output_file, "# Types: NEW_PROCESS, HYPERCALL_ENTRY, SYSCALL_ENTRY, PROCESS_SUMMARY\n\n");
}

/* Write process summary */
void write_process_summary(void)
{
    fprintf(g_tracer->output_file, "\n# PROCESS SUMMARY\n");
    
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, g_tracer->processes);
    
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        process_info_t *proc = (process_info_t *)value;
        fprintf(g_tracer->output_file, "PROCESS_SUMMARY: PID=%lu NAME=%s HYPERCALLS=%lu SYSCALLS=%lu DURATION=%ld CR3=0x%lx\n",
                proc->pid, proc->name, proc->hypercall_count, proc->syscall_count,
                proc->last_seen - proc->first_seen, proc->cr3);
    }
}

void print_statistics(void)
{
    printf("\n=== HYPERTRACER STATISTICS ===\n");
    printf("Total processes tracked: %u\n", g_hash_table_size(g_tracer->processes));
    printf("Total hypercalls traced: %lu\n", g_tracer->hypercall_index);
    printf("Total syscalls traced: %lu\n", g_tracer->syscall_index);
    printf("Trace duration: %ld seconds\n", time(NULL) - g_tracer->start_time);
    
    // Show top processes by hypercall activity
    printf("\nTop processes by hypercall activity:\n");
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, g_tracer->processes);
    
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        process_info_t *proc = (process_info_t *)value;
        if (proc->hypercall_count > 0) {
            printf("  PID %lu (%s): %lu hypercalls, %lu syscalls\n",
                   proc->pid, proc->name, proc->hypercall_count, proc->syscall_count);
        }
    }
}

/* Generate detailed report */
void generate_report(void)
{
    FILE *report = fopen("hypertracer_report.html", "w");
    if (!report) return;
    
    fprintf(report, "<html><head><title>Hypertracer Report</title></head><body>\n");
    fprintf(report, "<h1>QEMU Hypertracer Report</h1>\n");
    fprintf(report, "<p>Generated: %s</p>\n", ctime(&g_tracer->start_time));
    
    /* Process table */
    fprintf(report, "<h2>Process Summary</h2>\n");
    fprintf(report, "<table border='1'>\n");
    fprintf(report, "<tr><th>PID</th><th>Name</th><th>Hypercalls</th><th>Syscalls</th><th>CR3</th></tr>\n");
    
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, g_tracer->processes);
    
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        process_info_t *proc = (process_info_t *)value;
        fprintf(report, "<tr><td>%lu</td><td>%s</td><td>%lu</td><td>%lu</td><td>0x%lx</td></tr>\n",
                proc->pid, proc->name, proc->hypercall_count, proc->syscall_count, proc->cr3);
    }
    
    fprintf(report, "</table>\n");
    fprintf(report, "</body></html>\n");
    fclose(report);
}

/* Cleanup allocated data */
void cleanup_data(void)
{
    if (g_tracer) {
        if (g_tracer->processes) {
            g_hash_table_destroy(g_tracer->processes);
        }
        if (g_tracer->cr3_to_pid) {
            g_hash_table_destroy(g_tracer->cr3_to_pid);
        }
        if (g_tracer->hypercalls) {
            g_free(g_tracer->hypercalls);
        }
        if (g_tracer->syscalls) {
            g_free(g_tracer->syscalls);
        }
        if (g_tracer->output_file) {
            fclose(g_tracer->output_file);
        }
        g_free(g_tracer);
        g_tracer = NULL;
    }
}
