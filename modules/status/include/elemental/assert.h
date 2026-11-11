/*
 * elemental/assert.h - contract assertions, for module and backend sources.
 *
 * EL_ASSERT(condition) states something that is true unless the program
 * has a bug: a caller broke a documented precondition, or a backend broke
 * its interface contract. It is not error handling. A condition that can be
 * false in a correct program is reported with an el_status instead.
 *
 * This header is for code that implements Elemental interfaces. Unlike every
 * other public header, its behavior depends on the configuration, which it
 * reads from macros the including source file has already defined:
 *
 *     #include "elemental_config.h"   // first, in every implementation file
 *     #include <elemental/assert.h>
 *
 *   EL_CONFIG_ASSERT                      checks are compiled in
 *   EL_CONFIG_ASSERT_HANDLER_TRAP         a failed check traps in place
 *   EL_CONFIG_ASSERT_HANDLER_APPLICATION  a failed check calls
 *                                           el_assert_failed(), see
 *                                           elemental/assert_handler.h
 *
 * With checks compiled out the condition is not evaluated, but it is still
 * parsed and type-checked, so it cannot rot and it must not have side
 * effects the program depends on.
 */
#ifndef ELEMENTAL_ASSERT_H
#define ELEMENTAL_ASSERT_H

#ifndef EL_CONFIG_STATUS
#error \
    "Include elemental_config.h before <elemental/assert.h>. This header is for sources that implement Elemental modules and backends."
#endif

/* EL_NORETURN and the el_assert_failed() handler. */
#include <elemental/assert_handler.h>

#if defined(__GNUC__) || defined(__clang__)
#define EL_TRAP() __builtin_trap()
#elif defined(_MSC_VER)
#define EL_TRAP() __fastfail(7 /* FAST_FAIL_FATAL_APP_EXIT */)
#else
/* No trap primitive known for this compiler: stop here, visibly. */
EL_NORETURN static inline void el_trap_(void)
{
    for (;;) {
    }
}
#define EL_TRAP() el_trap_()
#endif

/*
 * The file name a failed check reports. The base name where the compiler
 * offers it: it is shorter, and unlike __FILE__ it does not put the build
 * machine's directory layout into the image.
 */
#ifdef __FILE_NAME__
#define EL_ASSERT_FILE __FILE_NAME__
#else
#define EL_ASSERT_FILE __FILE__
#endif

#if !defined(EL_CONFIG_ASSERT)
#define EL_ASSERT(condition) ((void)sizeof((condition) ? 1 : 0))
#elif defined(EL_CONFIG_ASSERT_HANDLER_APPLICATION)
#define EL_ASSERT(condition) ((condition) ? (void)0 : el_assert_failed(EL_ASSERT_FILE, __LINE__))
#else
#define EL_ASSERT(condition) ((condition) ? (void)0 : EL_TRAP())
#endif

#endif /* ELEMENTAL_ASSERT_H */
