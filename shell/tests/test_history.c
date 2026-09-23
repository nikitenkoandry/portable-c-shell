#include "test_common.h"

int main(void)
{
    test_shell_t ts;
    test_shell_init(&ts);

    ASSERT_EQ_INT(SH_HISTORY_DEFAULT_DEPTH, (int)sh_history_capacity(&ts.shell));
    ASSERT_EQ_INT(0, (int)sh_history_count(&ts.shell));

    sh_execute_line(&ts.shell, "missing one");
    sh_execute_line(&ts.shell, "missing two");
    ASSERT_EQ_INT(2, (int)sh_history_count(&ts.shell));
    ASSERT_STREQ("missing two", sh_history_get_newest(&ts.shell, 0));
    ASSERT_STREQ("missing one", sh_history_get_newest(&ts.shell, 1));

    sh_history_clear(&ts.shell);
    ASSERT_EQ_INT(0, (int)sh_history_count(&ts.shell));
    return 0;
}
