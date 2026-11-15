/* Compiled by the status.nodiscard_check.*.rejects tests, as C and as C++:
 * a call that drops the status of an EL_NODISCARD function must be
 * diagnosed. */
#include <elemental/status.h>

EL_NODISCARD el_status nodiscard_check_step(int x);
void nodiscard_check(int x);

void nodiscard_check(int x)
{
    nodiscard_check_step(x);
}
