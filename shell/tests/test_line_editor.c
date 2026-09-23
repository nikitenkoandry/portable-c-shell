#include "test_common.h"

int main(void)
{
    test_shell_t ts;
    test_shell_init(&ts);

    sh_execute_line(&ts.shell, "first");
    sh_execute_line(&ts.shell, "second");

    sh_input_byte(&ts.shell, 'd');
    sh_input_byte(&ts.shell, 'r');
    sh_input_byte(&ts.shell, '\x1b');
    sh_input_byte(&ts.shell, '[');
    sh_input_byte(&ts.shell, 'A');
    ASSERT_STREQ("second", ts.shell.line);

    sh_input_byte(&ts.shell, '\x1b');
    sh_input_byte(&ts.shell, '[');
    sh_input_byte(&ts.shell, 'A');
    ASSERT_STREQ("first", ts.shell.line);

    sh_input_byte(&ts.shell, '\x1b');
    sh_input_byte(&ts.shell, '[');
    sh_input_byte(&ts.shell, 'B');
    ASSERT_STREQ("second", ts.shell.line);

    sh_input_byte(&ts.shell, '\x1b');
    sh_input_byte(&ts.shell, '[');
    sh_input_byte(&ts.shell, 'B');
    ASSERT_STREQ("dr", ts.shell.line);
    return 0;
}
