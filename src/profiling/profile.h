#ifndef SPWKIT_PROFILING_PROFILE_H
#define SPWKIT_PROFILING_PROFILE_H

#include <stdint.h>

#ifndef SPWKIT_ENABLE_PROFILING
#define SPWKIT_ENABLE_PROFILING 0
#endif

#ifndef SPWKIT_PROFILE_COUNTER_HZ
#define SPWKIT_PROFILE_COUNTER_HZ 0
#endif

#define SPW_PROFILE_ID_TX_API_ENTRY 1
#define SPW_PROFILE_ID_TX_BACKEND_ENTRY 2
#define SPW_PROFILE_ID_TX_PROVIDER_ENTRY 3
#define SPW_PROFILE_ID_TX_PROVIDER_BOUNDARY 4
#define SPW_PROFILE_ID_RX_PROVIDER_BOUNDARY 5
#define SPW_PROFILE_ID_RX_PROVIDER_RETURN 6
#define SPW_PROFILE_ID_RX_BACKEND_RETURN 7
#define SPW_PROFILE_ID_RX_API_RETURN 8

#if SPWKIT_ENABLE_PROFILING

#ifndef SPWKIT_PROFILE_START
#error "SPWKIT_PROFILE_START must name one SPW_PROFILE_ID_* probe"
#endif
#ifndef SPWKIT_PROFILE_END
#error "SPWKIT_PROFILE_END must name one SPW_PROFILE_ID_* probe"
#endif

static inline uint64_t spw_profile_delta32(uint64_t start, uint64_t end) {
    return (uint64_t)((uint32_t)end - (uint32_t)start);
}

static inline uint64_t spw_profile_delta64(uint64_t start, uint64_t end) {
    return end - start;
}

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#include <emmintrin.h>
#define SPW_PROFILE_COUNTER_WIDTH_BITS 64u
static __forceinline uint64_t spw_profile_counter_read(void) {
    unsigned int aux = 0u;
    const uint64_t value = __rdtscp(&aux);
    _mm_lfence();
    return value;
}
static __forceinline void spw_profile_counter_prepare(void) {
    _mm_lfence();
}
static __forceinline uint64_t spw_profile_counter_frequency_hz(void) {
#if SPWKIT_PROFILE_COUNTER_HZ > 0
    return (uint64_t)SPWKIT_PROFILE_COUNTER_HZ;
#else
    return 0u;
#endif
}
static __forceinline const char* spw_profile_counter_kind(void) {
    return "x86-rdtscp";
}
#elif (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
#define SPW_PROFILE_COUNTER_WIDTH_BITS 64u
static __inline__ __attribute__((always_inline)) uint64_t spw_profile_counter_read(void) {
    uint32_t low = 0u;
    uint32_t high = 0u;
    uint32_t aux = 0u;
    __asm__ __volatile__("rdtscp" : "=a"(low), "=d"(high), "=c"(aux) :: "memory");
    __asm__ __volatile__("lfence" ::: "memory");
    return ((uint64_t)high << 32u) | (uint64_t)low;
}
static __inline__ __attribute__((always_inline)) void spw_profile_counter_prepare(void) {
    __asm__ __volatile__("lfence" ::: "memory");
}
static __inline__ __attribute__((always_inline)) uint64_t spw_profile_counter_frequency_hz(void) {
#if SPWKIT_PROFILE_COUNTER_HZ > 0
    return (uint64_t)SPWKIT_PROFILE_COUNTER_HZ;
#else
    return 0u;
#endif
}
static __inline__ __attribute__((always_inline)) const char* spw_profile_counter_kind(void) {
    return "x86-rdtscp";
}
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
#define SPW_PROFILE_COUNTER_WIDTH_BITS 64u
static __inline__ __attribute__((always_inline)) uint64_t spw_profile_counter_read(void) {
    uint64_t value = 0u;
    __asm__ __volatile__("isb\n\tmrs %0, cntvct_el0" : "=r"(value) :: "memory");
    return value;
}
static __inline__ __attribute__((always_inline)) void spw_profile_counter_prepare(void) {
    __asm__ __volatile__("isb" ::: "memory");
}
static __inline__ __attribute__((always_inline)) uint64_t spw_profile_counter_frequency_hz(void) {
#if SPWKIT_PROFILE_COUNTER_HZ > 0
    return (uint64_t)SPWKIT_PROFILE_COUNTER_HZ;
#else
    uint64_t value = 0u;
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(value));
    return value;
#endif
}
static __inline__ __attribute__((always_inline)) const char* spw_profile_counter_kind(void) {
    return "aarch64-cntvct";
}
#elif (defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_8M_MAIN__)) && (defined(__GNUC__) || defined(__clang__))
#define SPW_PROFILE_COUNTER_WIDTH_BITS 32u
#define SPW_PROFILE_DEMCR (*(volatile uint32_t*)0xE000EDFCu)
#define SPW_PROFILE_DWT_CTRL (*(volatile uint32_t*)0xE0001000u)
#define SPW_PROFILE_DWT_CYCCNT (*(volatile uint32_t*)0xE0001004u)
#define SPW_PROFILE_DEMCR_TRCENA (1u << 24u)
#define SPW_PROFILE_DWT_CTRL_CYCCNTENA (1u << 0u)
static __inline__ __attribute__((always_inline)) uint64_t spw_profile_counter_read(void) {
    __asm__ __volatile__("" ::: "memory");
    return (uint64_t)SPW_PROFILE_DWT_CYCCNT;
}
static __inline__ __attribute__((always_inline)) void spw_profile_counter_prepare(void) {
    SPW_PROFILE_DEMCR |= SPW_PROFILE_DEMCR_TRCENA;
    SPW_PROFILE_DWT_CYCCNT = 0u;
    SPW_PROFILE_DWT_CTRL |= SPW_PROFILE_DWT_CTRL_CYCCNTENA;
    __asm__ __volatile__("" ::: "memory");
}
static __inline__ __attribute__((always_inline)) uint64_t spw_profile_counter_frequency_hz(void) {
#if SPWKIT_PROFILE_COUNTER_HZ > 0
    return (uint64_t)SPWKIT_PROFILE_COUNTER_HZ;
#else
    return 0u;
#endif
}
static __inline__ __attribute__((always_inline)) const char* spw_profile_counter_kind(void) {
    return "cortex-m-dwt-cyccnt";
}
#else
#error "SpWKit profiling has no cycle-counter backend for this architecture/compiler"
#endif

static inline uint32_t spw_profile_counter_width_bits(void) {
    return SPW_PROFILE_COUNTER_WIDTH_BITS;
}

static inline uint64_t spw_profile_counter_delta(uint64_t start, uint64_t end) {
#if SPW_PROFILE_COUNTER_WIDTH_BITS == 32u
    /* Correct modulo-2^32 delta for one unambiguous DWT wrap interval. */
    return spw_profile_delta32(start, end);
#else
    return spw_profile_delta64(start, end);
#endif
}

typedef struct spw_profile_sample {
    uint64_t start;
    uint64_t end;
    uint64_t delta;
    uint32_t sequence;
} spw_profile_sample_t;

extern volatile spw_profile_sample_t spw_profile_state;

void spw_profile_prepare(void);
void spw_profile_reset(void);
const volatile spw_profile_sample_t* spw_profile_last_sample(void);

#define SPW_PROFILE_CAPTURE_START() \
    do { \
        spw_profile_state.start = spw_profile_counter_read(); \
    } while (0)

#define SPW_PROFILE_CAPTURE_END() \
    do { \
        const uint64_t spw_profile_end_value_ = spw_profile_counter_read(); \
        spw_profile_state.end = spw_profile_end_value_; \
        spw_profile_state.delta = \
            spw_profile_counter_delta(spw_profile_state.start, spw_profile_end_value_); \
        spw_profile_state.sequence += 1u; \
    } while (0)

#if SPWKIT_PROFILE_START == SPW_PROFILE_ID_TX_API_ENTRY
#define SPW_PROFILE_TX_API_ENTRY() SPW_PROFILE_CAPTURE_START()
#elif SPWKIT_PROFILE_END == SPW_PROFILE_ID_TX_API_ENTRY
#define SPW_PROFILE_TX_API_ENTRY() SPW_PROFILE_CAPTURE_END()
#else
#define SPW_PROFILE_TX_API_ENTRY() ((void)0)
#endif

#if SPWKIT_PROFILE_START == SPW_PROFILE_ID_TX_BACKEND_ENTRY
#define SPW_PROFILE_TX_BACKEND_ENTRY() SPW_PROFILE_CAPTURE_START()
#elif SPWKIT_PROFILE_END == SPW_PROFILE_ID_TX_BACKEND_ENTRY
#define SPW_PROFILE_TX_BACKEND_ENTRY() SPW_PROFILE_CAPTURE_END()
#else
#define SPW_PROFILE_TX_BACKEND_ENTRY() ((void)0)
#endif

#if SPWKIT_PROFILE_START == SPW_PROFILE_ID_TX_PROVIDER_ENTRY
#define SPW_PROFILE_TX_PROVIDER_ENTRY() SPW_PROFILE_CAPTURE_START()
#elif SPWKIT_PROFILE_END == SPW_PROFILE_ID_TX_PROVIDER_ENTRY
#define SPW_PROFILE_TX_PROVIDER_ENTRY() SPW_PROFILE_CAPTURE_END()
#else
#define SPW_PROFILE_TX_PROVIDER_ENTRY() ((void)0)
#endif

#if SPWKIT_PROFILE_START == SPW_PROFILE_ID_TX_PROVIDER_BOUNDARY
#define SPW_PROFILE_TX_PROVIDER_BOUNDARY() SPW_PROFILE_CAPTURE_START()
#elif SPWKIT_PROFILE_END == SPW_PROFILE_ID_TX_PROVIDER_BOUNDARY
#define SPW_PROFILE_TX_PROVIDER_BOUNDARY() SPW_PROFILE_CAPTURE_END()
#else
#define SPW_PROFILE_TX_PROVIDER_BOUNDARY() ((void)0)
#endif

#if SPWKIT_PROFILE_START == SPW_PROFILE_ID_RX_PROVIDER_BOUNDARY
#define SPW_PROFILE_RX_PROVIDER_BOUNDARY() SPW_PROFILE_CAPTURE_START()
#elif SPWKIT_PROFILE_END == SPW_PROFILE_ID_RX_PROVIDER_BOUNDARY
#define SPW_PROFILE_RX_PROVIDER_BOUNDARY() SPW_PROFILE_CAPTURE_END()
#else
#define SPW_PROFILE_RX_PROVIDER_BOUNDARY() ((void)0)
#endif

#if SPWKIT_PROFILE_START == SPW_PROFILE_ID_RX_PROVIDER_RETURN
#define SPW_PROFILE_RX_PROVIDER_RETURN() SPW_PROFILE_CAPTURE_START()
#elif SPWKIT_PROFILE_END == SPW_PROFILE_ID_RX_PROVIDER_RETURN
#define SPW_PROFILE_RX_PROVIDER_RETURN() SPW_PROFILE_CAPTURE_END()
#else
#define SPW_PROFILE_RX_PROVIDER_RETURN() ((void)0)
#endif

#if SPWKIT_PROFILE_START == SPW_PROFILE_ID_RX_BACKEND_RETURN
#define SPW_PROFILE_RX_BACKEND_RETURN() SPW_PROFILE_CAPTURE_START()
#elif SPWKIT_PROFILE_END == SPW_PROFILE_ID_RX_BACKEND_RETURN
#define SPW_PROFILE_RX_BACKEND_RETURN() SPW_PROFILE_CAPTURE_END()
#else
#define SPW_PROFILE_RX_BACKEND_RETURN() ((void)0)
#endif

#if SPWKIT_PROFILE_START == SPW_PROFILE_ID_RX_API_RETURN
#define SPW_PROFILE_RX_API_RETURN() SPW_PROFILE_CAPTURE_START()
#elif SPWKIT_PROFILE_END == SPW_PROFILE_ID_RX_API_RETURN
#define SPW_PROFILE_RX_API_RETURN() SPW_PROFILE_CAPTURE_END()
#else
#define SPW_PROFILE_RX_API_RETURN() ((void)0)
#endif

#else

#define SPW_PROFILE_TX_API_ENTRY() ((void)0)
#define SPW_PROFILE_TX_BACKEND_ENTRY() ((void)0)
#define SPW_PROFILE_TX_PROVIDER_ENTRY() ((void)0)
#define SPW_PROFILE_TX_PROVIDER_BOUNDARY() ((void)0)
#define SPW_PROFILE_RX_PROVIDER_BOUNDARY() ((void)0)
#define SPW_PROFILE_RX_PROVIDER_RETURN() ((void)0)
#define SPW_PROFILE_RX_BACKEND_RETURN() ((void)0)
#define SPW_PROFILE_RX_API_RETURN() ((void)0)

#endif

#endif
