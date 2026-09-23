#include "sh_shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ASSERT_TRUE(x) do { if (!(x)) { \
    fprintf(stderr, "assert failed: %s:%d: %s\n", __FILE__, __LINE__, #x); \
    exit(1); \
} } while (0)
#define ASSERT_EQ_INT(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_STREQ(a, b) ASSERT_TRUE(strcmp((a), (b)) == 0)

static void test_empty_and_whitespace(void)
{
    char empty[] = "";
    char whitespace[] = " \t\r\n\v\f ";
    char *argv[4];
    int argc = 99;

    ASSERT_EQ_INT(SH_OK, sh_tokenize(empty, argv, ARRAY_SIZE(argv), &argc));
    ASSERT_EQ_INT(0, argc);

    argc = 99;
    ASSERT_EQ_INT(SH_OK, sh_tokenize(whitespace, argv, ARRAY_SIZE(argv), &argc));
    ASSERT_EQ_INT(0, argc);
}

static void test_repeated_whitespace(void)
{
    char line[] = "  wifi\t\tset   ssid\r\n";
    char *argv[4];
    int argc = 0;

    ASSERT_EQ_INT(SH_OK, sh_tokenize(line, argv, ARRAY_SIZE(argv), &argc));
    ASSERT_EQ_INT(3, argc);
    ASSERT_STREQ("wifi", argv[0]);
    ASSERT_STREQ("set", argv[1]);
    ASSERT_STREQ("ssid", argv[2]);
}

static void test_double_and_single_quotes(void)
{
    char line[] = "wifi set ssid \"My Home WiFi\" 'lab network'";
    char *argv[5];
    int argc = 0;

    ASSERT_EQ_INT(SH_OK, sh_tokenize(line, argv, ARRAY_SIZE(argv), &argc));
    ASSERT_EQ_INT(5, argc);
    ASSERT_STREQ("wifi", argv[0]);
    ASSERT_STREQ("set", argv[1]);
    ASSERT_STREQ("ssid", argv[2]);
    ASSERT_STREQ("My Home WiFi", argv[3]);
    ASSERT_STREQ("lab network", argv[4]);
}

static void test_empty_quoted_arguments(void)
{
    char line[] = "cmd \"\" '' end";
    char *argv[4];
    int argc = 0;

    ASSERT_EQ_INT(SH_OK, sh_tokenize(line, argv, ARRAY_SIZE(argv), &argc));
    ASSERT_EQ_INT(4, argc);
    ASSERT_STREQ("cmd", argv[0]);
    ASSERT_STREQ("", argv[1]);
    ASSERT_STREQ("", argv[2]);
    ASSERT_STREQ("end", argv[3]);
}

static void test_escaped_characters(void)
{
    char line[] = "cmd a\\ b \\\"quoted\\\" it\\'s slash\\\\value \"double\\ space\" 'single\\backslash'";
    char *argv[7];
    int argc = 0;

    ASSERT_EQ_INT(SH_OK, sh_tokenize(line, argv, ARRAY_SIZE(argv), &argc));
    ASSERT_EQ_INT(7, argc);
    ASSERT_STREQ("cmd", argv[0]);
    ASSERT_STREQ("a b", argv[1]);
    ASSERT_STREQ("\"quoted\"", argv[2]);
    ASSERT_STREQ("it's", argv[3]);
    ASSERT_STREQ("slash\\value", argv[4]);
    ASSERT_STREQ("double space", argv[5]);
    ASSERT_STREQ("single\\backslash", argv[6]);
}

static void test_escapes_inside_quotes(void)
{
    char line[] = "cmd \"say \\\"hello\\\"\" \"C:\\\\temp\" 'trailing\\'";
    char *argv[4];
    int argc = 0;

    ASSERT_EQ_INT(SH_OK, sh_tokenize(line, argv, ARRAY_SIZE(argv), &argc));
    ASSERT_EQ_INT(4, argc);
    ASSERT_STREQ("cmd", argv[0]);
    ASSERT_STREQ("say \"hello\"", argv[1]);
    ASSERT_STREQ("C:\\temp", argv[2]);
    ASSERT_STREQ("trailing\\", argv[3]);
}

static void test_concatenated_fragments(void)
{
    char line[] = "cmd pre\"middle part\"'post' a''b \"\"suffix \"it's\" 'say \"hi\"'";
    char *argv[6];
    int argc = 0;

    ASSERT_EQ_INT(SH_OK, sh_tokenize(line, argv, ARRAY_SIZE(argv), &argc));
    ASSERT_EQ_INT(6, argc);
    ASSERT_STREQ("cmd", argv[0]);
    ASSERT_STREQ("premiddle partpost", argv[1]);
    ASSERT_STREQ("ab", argv[2]);
    ASSERT_STREQ("suffix", argv[3]);
    ASSERT_STREQ("it's", argv[4]);
    ASSERT_STREQ("say \"hi\"", argv[5]);
}

static void test_exact_argument_capacity(void)
{
    char exact[] = "a b c d";
    char overflow[] = "a b c d e";
    char *argv[4];
    int argc = 0;

    ASSERT_EQ_INT(SH_OK, sh_tokenize(exact, argv, ARRAY_SIZE(argv), &argc));
    ASSERT_EQ_INT(4, argc);
    ASSERT_STREQ("a", argv[0]);
    ASSERT_STREQ("d", argv[3]);

    argc = 99;
    ASSERT_EQ_INT(SH_ERR_TOO_MANY_ARGS,
                  sh_tokenize(overflow, argv, ARRAY_SIZE(argv), &argc));
    ASSERT_EQ_INT(0, argc);
}

static void test_unterminated_quotes(void)
{
    char double_quote[] = "cmd \"unterminated";
    char single_quote[] = "cmd 'unterminated";
    char after_fragment[] = "cmd prefix\"unterminated";
    char *argv[4];
    int argc = 99;

    ASSERT_EQ_INT(SH_ERR_UNTERMINATED_QUOTE,
                  sh_tokenize(double_quote, argv, ARRAY_SIZE(argv), &argc));
    ASSERT_EQ_INT(0, argc);

    argc = 99;
    ASSERT_EQ_INT(SH_ERR_UNTERMINATED_QUOTE,
                  sh_tokenize(single_quote, argv, ARRAY_SIZE(argv), &argc));
    ASSERT_EQ_INT(0, argc);

    argc = 99;
    ASSERT_EQ_INT(SH_ERR_UNTERMINATED_QUOTE,
                  sh_tokenize(after_fragment, argv, ARRAY_SIZE(argv), &argc));
    ASSERT_EQ_INT(0, argc);
}

static void test_invalid_arguments_and_dangling_escape(void)
{
    char valid[] = "cmd";
    char trailing_escape[] = "cmd trailing\\";
    char quoted_trailing_escape[] = "cmd \"trailing\\";
    char *argv[2];
    int argc = 99;

    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  sh_tokenize(NULL, argv, ARRAY_SIZE(argv), &argc));
    ASSERT_EQ_INT(0, argc);

    argc = 99;
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  sh_tokenize(valid, NULL, ARRAY_SIZE(argv), &argc));
    ASSERT_EQ_INT(0, argc);

    argc = 99;
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG, sh_tokenize(valid, argv, 0u, &argc));
    ASSERT_EQ_INT(0, argc);

    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  sh_tokenize(valid, argv, ARRAY_SIZE(argv), NULL));

    argc = 99;
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  sh_tokenize(trailing_escape, argv, ARRAY_SIZE(argv), &argc));
    ASSERT_EQ_INT(0, argc);

    argc = 99;
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  sh_tokenize(quoted_trailing_escape, argv, ARRAY_SIZE(argv), &argc));
    ASSERT_EQ_INT(0, argc);
}

static void test_error_strings(void)
{
    ASSERT_STREQ("invalid argument", sh_status_string(SH_ERR_INVALID_ARG));
    ASSERT_STREQ("too many arguments", sh_status_string(SH_ERR_TOO_MANY_ARGS));
    ASSERT_STREQ("unterminated quote", sh_status_string(SH_ERR_UNTERMINATED_QUOTE));
}

int main(void)
{
    test_empty_and_whitespace();
    test_repeated_whitespace();
    test_double_and_single_quotes();
    test_empty_quoted_arguments();
    test_escaped_characters();
    test_escapes_inside_quotes();
    test_concatenated_fragments();
    test_exact_argument_capacity();
    test_unterminated_quotes();
    test_invalid_arguments_and_dangling_escape();
    test_error_strings();

    return 0;
}
