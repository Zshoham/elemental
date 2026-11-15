/* Compiled by the status.nodiscard_check.*.accepts tests, as C and as C++:
 * each way of using a must-use status produces no diagnostic. */
#include <elemental/status.h>

EL_NODISCARD el_status nodiscard_check_step(int x);
el_status nodiscard_check(int x);

el_status nodiscard_check(int x)
{
    /* Acted on. */
    if (nodiscard_check_step(x) == EL_ERR_WOULD_BLOCK) {
        return EL_OK;
    }
    /* Passed on. */
    EL_TRY(nodiscard_check_step(x));
    /* Discarded on purpose. */
    el_status_ignore(nodiscard_check_step(x));
    /* Returned. */
    return nodiscard_check_step(x);
}
