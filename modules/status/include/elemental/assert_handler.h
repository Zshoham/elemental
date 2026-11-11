/*
 * elemental/assert_handler.h - the assertion failure handler an application
 * provides.
 *
 * Only relevant when Elemental is configured with
 * ASSERT_HANDLER_APPLICATION. In that configuration a failed contract
 * assertion inside Elemental calls el_assert_failed(), and the application
 * must define it exactly once:
 *
 *     #include <elemental/assert_handler.h>
 *
 *     void el_assert_failed(const char *file, int line)
 *     {
 *         log_fault(file, line);
 *         system_reset();
 *     }
 *
 * Linking fails with an undefined reference if it is missing. In the default
 * configuration (ASSERT_HANDLER_TRAP) Elemental never calls it.
 *
 * This header does not depend on the configuration.
 */
#ifndef ELEMENTAL_ASSERT_HANDLER_H
#define ELEMENTAL_ASSERT_HANDLER_H

#ifdef __cplusplus
#define EL_NORETURN [[noreturn]]
#else
#define EL_NORETURN _Noreturn
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Called when a contract assertion fails: a caller broke a documented
 * precondition, or a backend broke its interface contract. Either way the
 * program has a bug and the failing operation cannot continue.
 *
 * `file` names the source file of the failed check and has static storage
 * duration; `line` is its line number.
 *
 * Must not return. It can be called from any context an Elemental function
 * is called from, including an interrupt handler, so it must not block or
 * allocate. What it does instead of returning is the application's choice:
 * reset, halt, abort. Elemental's own tests abort.
 */
EL_NORETURN void el_assert_failed(const char *file, int line);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_ASSERT_HANDLER_H */
