#include "pintrace.h"

unsigned long long __attribute__((optimize("O0"))) add_mem(unsigned long long start_address, unsigned long long end_address) {
	return end_address - start_address;
}

unsigned long long __attribute__((optimize("O0"))) remove_mem(unsigned long long start_address, unsigned long long end_address) {
	return end_address - start_address;
}

bool __attribute__((optimize("O0"))) set_enable_trace(bool enable_trace) {
    return enable_trace;
}