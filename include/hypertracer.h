#ifndef HYPERTRACER_H
#define HYPERTRACER_H

#include <qemu-plugin.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

// Common hypercall numbers
#define HYPERCALL_UNKNOWN       0
#define HYPERCALL_MEMORY_OP     1
#define HYPERCALL_SCHED_OP      2
#define HYPERCALL_CONSOLE_IO    3
#define HYPERCALL_XEN_VERSION   4
#define HYPERCALL_EVENT_CHANNEL 5
#define HYPERCALL_PHYSDEV_OP    6
#define HYPERCALL_GRANT_TABLE   7
#define HYPERCALL_VM_ASSIST     8
#define HYPERCALL_UPDATE_VA_MAP 9
#define HYPERCALL_HVM_OP        10

#define MAX_PROCESSES 1024
#define MAX_PROCESS_NAME 256
#define MAX_HYPERCALLS_PER_PROCESS 10000

// Process information structure
typedef struct {
    uint64_t pid;
    uint64_t cr3;           // Page directory base 
    char name[MAX_PROCESS_NAME];
    uint64_t hypercall_count;
    uint64_t syscall_count;
    time_t first_seen;
    time_t last_seen;
    bool active;
} process_info_t;

// Hypercall trace entry
typedef struct {
    uint64_t timestamp;
    uint64_t pid;
    uint64_t hypercall_num;
    uint64_t arg1, arg2, arg3, arg4, arg5;
    uint64_t return_value;
    uint64_t pc;            
    bool completed;
} hypercall_trace_t;

// Syscall trace entry  
typedef struct {
    uint64_t timestamp;
    uint64_t pid;
    uint64_t syscall_num;
    uint64_t args[6];
    uint64_t return_value;
    uint64_t pc;
    bool completed;
} syscall_trace_t;

// Global data structure
typedef struct {
    GHashTable *processes;          
    GHashTable *cr3_to_pid;        
    hypercall_trace_t *hypercalls; 
    syscall_trace_t *syscalls;     
    uint64_t hypercall_index;
    uint64_t syscall_index;
    uint64_t current_pid;
    uint64_t current_cr3;
    FILE *output_file;
    bool trace_enabled;
    uint64_t start_time;
} tracer_data_t;

// Function declarations
void plugin_init(void);
void plugin_exit(qemu_plugin_id_t id, void *p);

// Process management
process_info_t* get_or_create_process(uint64_t pid, uint64_t cr3);
void update_process_name(uint64_t pid, const char *name);
uint64_t get_current_pid(void);
uint64_t get_current_cr3(void);

// Hypercall tracing
void trace_hypercall_entry(unsigned int vcpu_index, void *userdata);
void trace_hypercall_exit(unsigned int vcpu_index, void *userdata);
const char* get_hypercall_name(uint64_t hypercall_num);

// Syscall tracing
void trace_syscall_entry(unsigned int vcpu_index, void *userdata);
void trace_syscall_exit(unsigned int vcpu_index, void *userdata);

// Callbacks
void on_instruction_exec(unsigned int vcpu_index, void *userdata);
void on_memory_access(unsigned int vcpu_index, qemu_plugin_meminfo_t info, 
                     uint64_t vaddr, void *userdata);

// Utility functions
void write_trace_header(void);
void write_process_summary(void);
void write_hypercall_trace(hypercall_trace_t *trace);
void write_syscall_trace(syscall_trace_t *trace);
void cleanup_data(void);

// Architecture-specific functions
uint64_t read_guest_register(unsigned int vcpu_index, const char *reg_name);
bool is_hypercall_instruction(uint64_t pc, uint64_t insn);
bool is_syscall_instruction(uint64_t pc, uint64_t insn);

// Statistics
void print_statistics(void);
void generate_report(void);

extern tracer_data_t *g_tracer;

#endif /* HYPERTRACER_H */
