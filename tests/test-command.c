/* -*- c-basic-offset: 4; -*- */
#define _POSIX_SOURCE

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <webkit/webkit.h>
#include <libsoup/soup.h>
#include <JavaScriptCore/JavaScript.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>

#include <src/uzbl-core.h>
#include <src/config.h>
#include <src/type.h>
#include <src/variables.h>

extern UzblCore uzbl;

#define INSTANCE_NAME "testing"

#define ASSERT_EVENT(EF, STR) { read_event(ef); \
    g_assert_cmpstr("EVENT [" INSTANCE_NAME "] " STR "\n", ==, ef->event_buffer); }

struct EventFixture
{
  /* uzbl's end of the socketpair */
  int uzbl_sock;

  /* the test framework's end of the socketpair */
  int test_sock;
  char event_buffer[1024];
};

void
read_event (struct EventFixture *ef) {
    int r = read(ef->test_sock, ef->event_buffer, 1023); \
    ef->event_buffer[r] = 0;
}

void
assert_no_event (struct EventFixture *ef) {
    fd_set rfds;

    FD_ZERO(&rfds);
    FD_SET(ef->test_sock, &rfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    /* check if there's any data waiting */
    int res = select(ef->test_sock + 1, &rfds, NULL, NULL, &timeout);

    if(res == 0) {
        /* timeout expired, there was no event */

        /* success */
        return;
    } else if(res == -1) {
        /* mechanical failure */
        perror("select():");
        assert(0);
    } else {
        /* there was an event. display it. */
        read_event(ef);
        g_assert_cmpstr("", ==, ef->event_buffer);
    }
}

void
event_fixture_setup(struct EventFixture *ef, const void* data)
{
    (void) data;

    int socks[2];

    /* make some sockets, fresh for every test */
    if(socketpair(AF_UNIX, SOCK_STREAM, 0, socks) == -1)
    {
      perror("socketpair() failed");
      g_assert(0);
    }

    ef->uzbl_sock = socks[0];
    ef->test_sock = socks[1];

    /* attach uzbl_sock to uzbl's event dispatcher. */
    GIOChannel *iochan = g_io_channel_unix_new(ef->uzbl_sock);
    g_io_channel_set_encoding(iochan, NULL, NULL);

    if(!uzbl.comm.connect_chan)
        uzbl.comm.connect_chan = g_ptr_array_new();
    if(!uzbl.comm.client_chan)
        uzbl.comm.client_chan = g_ptr_array_new();
    g_ptr_array_add(uzbl.comm.client_chan, (gpointer)iochan);
}

void
event_fixture_teardown(struct EventFixture *ef, const void *data)
{
    (void) data;

    /* there should be no events left waiting */
    assert_no_event(ef);

    /* clean up the io channel we opened for uzbl */
    GIOChannel *iochan = g_ptr_array_index(uzbl.comm.client_chan, 0);
    remove_socket_from_array(iochan);

    /* close the sockets so that nothing sticks around between tests */
    close(ef->uzbl_sock);
    close(ef->test_sock);
}

/* actual tests begin here */

void
test_event (struct EventFixture *ef, const void *data) {
    (void) data;

    parse_cmd_line("event", NULL);
    assert_no_event(ef);

    /* a simple event can be sent */
    parse_cmd_line("event event_type arg u ments", NULL);
    ASSERT_EVENT(ef, "EVENT_TYPE arg u ments");

    /* arguments to event should be expanded */
    parse_cmd_line("event event_type @(echo expansion)@ test", NULL);
    ASSERT_EVENT(ef, "EVENT_TYPE expansion test");

    /* "request" is just an alias for "event" */
    parse_cmd_line("request event_type arg u ments", NULL);
    ASSERT_EVENT(ef, "EVENT_TYPE arg u ments");
}


void
test_set_variable (struct EventFixture *ef, const void *data) {
    (void) data;

    /* set a string */
    parse_cmd_line("set useragent = Uzbl browser kthxbye!", NULL);
    ASSERT_EVENT(ef, "VARIABLE_SET useragent str 'Uzbl browser kthxbye!'");
    g_assert_cmpstr("Uzbl browser kthxbye!", ==, uzbl.net.useragent);

    /* set an int */
    parse_cmd_line("set forward_keys = 0", NULL);
    ASSERT_EVENT(ef, "VARIABLE_SET forward_keys int 0");
    g_assert_cmpint(0, ==, uzbl.behave.forward_keys);

    /* set a float */
    /* we have to be careful about locales here */
    GString *cmd;
    cmd = g_string_new("set zoom_level = ");
    g_string_append_printf(cmd, "%f", 0.25);
    parse_cmd_line(g_string_free(cmd, FALSE), NULL);

    ASSERT_EVENT(ef, "VARIABLE_SET zoom_level float 0.25");

    g_assert_cmpfloat(0.25, ==, get_var_value_float("zoom_level"));

    /* set a constant int (nothing should happen) */
    int old_major = uzbl.info.webkit_major;
    parse_cmd_line("set WEBKIT_MAJOR = 100", NULL);
    assert_no_event(ef);
    g_assert_cmpint(old_major, ==, uzbl.info.webkit_major);

    /* set a constant str (nothing should happen)  */
    GString *old_arch = g_string_new(uzbl.info.arch);
    parse_cmd_line("set ARCH_UZBL = A Lisp Machine", NULL);
    assert_no_event(ef);
    g_assert_cmpstr(g_string_free(old_arch, FALSE), ==, uzbl.info.arch);

    /* set a custom variable */
    parse_cmd_line("set nonexistant_variable = Some Value", NULL);
    ASSERT_EVENT(ef, "VARIABLE_SET nonexistant_variable str 'Some Value'");
    uzbl_cmdprop *c = g_hash_table_lookup(uzbl.behave.proto_var, "nonexistant_variable");
    g_assert_cmpstr("Some Value", ==, *(c->ptr.s));

    /* set a custom variable with expansion */
    parse_cmd_line("set an_expanded_variable = Test @(echo expansion)@", NULL);
    ASSERT_EVENT(ef, "VARIABLE_SET an_expanded_variable str 'Test expansion'");
    c = g_hash_table_lookup(uzbl.behave.proto_var, "an_expanded_variable");
    g_assert_cmpstr("Test expansion", ==, *(c->ptr.s));
}

void
test_print (void) {
    GString *result = g_string_new("");

    /* a simple message can be returned as a result */
    parse_cmd_line("print A simple test", result);
    g_assert_cmpstr("A simple test", ==, result->str);

    /* arguments to print should be expanded */
    parse_cmd_line("print A simple @(echo expansion)@ test", result);
    g_assert_cmpstr("A simple expansion test", ==, result->str);

    g_string_free(result, TRUE);
}

void
test_scroll (void) {
    GtkScrollbar *scbar_v = (GtkScrollbar*) gtk_vscrollbar_new (NULL);
    uzbl.gui.bar_v = gtk_range_get_adjustment((GtkRange*) scbar_v);

    gtk_adjustment_set_lower(uzbl.gui.bar_v, 0);
    gtk_adjustment_set_upper(uzbl.gui.bar_v, 100);
    gtk_adjustment_set_page_size(uzbl.gui.bar_v, 5);

    /* scroll vertical end should scroll it to upper - page_size */
    parse_cmd_line("scroll vertical end", NULL);
    g_assert_cmpfloat(gtk_adjustment_get_value(uzbl.gui.bar_v), ==, 95);

    /* scroll vertical begin should scroll it to lower */
    parse_cmd_line("scroll vertical begin", NULL);
    g_assert_cmpfloat(gtk_adjustment_get_value(uzbl.gui.bar_v), ==, 0);

    /* scroll vertical can scroll by pixels */
    parse_cmd_line("scroll vertical 15", NULL);
    g_assert_cmpfloat(gtk_adjustment_get_value(uzbl.gui.bar_v), ==, 15);

    parse_cmd_line("scroll vertical -10", NULL);
    g_assert_cmpfloat(gtk_adjustment_get_value(uzbl.gui.bar_v), ==, 5);

    /* scroll vertical can scroll by a percentage of the page size */
    parse_cmd_line("scroll vertical 100%", NULL);
    g_assert_cmpfloat(gtk_adjustment_get_value(uzbl.gui.bar_v), ==, 10);

    parse_cmd_line("scroll vertical 150%", NULL);
    g_assert_cmpfloat(gtk_adjustment_get_value(uzbl.gui.bar_v), ==, 17.5);

    /* scroll_horz behaves basically the same way. */
}

void
test_toggle_status (void) {
    /* status bar is not shown (for whatever reason) */
    g_assert(!get_show_status());

    /* status bar can be toggled on */
    parse_cmd_line("toggle show_status", NULL);
    g_assert(get_show_status());

    /* status bar can be toggled back off */
    parse_cmd_line("toggle show_status", NULL);
    g_assert(!get_show_status());
}

void
test_sync_sh (void) {
    GString *result = g_string_new("");

    parse_cmd_line("sync_sh 'echo Test echo.'", result);
    g_assert_cmpstr("Test echo.\n", ==, result->str);

    g_string_free(result, TRUE);
}

void
test_js (void) {
    GString *result = g_string_new("");

    /* simple javascript can be evaluated and returned */
    parse_cmd_line("js ('x' + 345).toUpperCase()", result);
    g_assert_cmpstr("X345", ==, result->str);

    g_string_free(result, TRUE);
}

void test_uri(void) {
    /* Testing for a crash, not crashing is a pass */
    parse_cmd_line("uri", NULL);
}

void
test_last_result (void) {
    GString *result = g_string_new("");

    /* the last result gets set */
    parse_cmd_line("js -1", result);
    g_assert_cmpstr("-1", ==, uzbl.state.last_result);

    /* the last result can be used in a chain */
    parse_cmd_line("chain 'js 1' 'js \\@_ + 1'", result);
    g_assert_cmpstr("2", ==, uzbl.state.last_result);

    g_string_free(result, TRUE);
}

void
test_no_such_command (void) {
    parse_cmd_line("no-such-command", NULL);
    /* if we didn't crash then we're ok! */
}

// TODO: test toggling emits an event
void
test_toggle_int (void) {
    g_assert_cmpint(0, ==, uzbl.behave.forward_keys);

    parse_cmd_line("toggle forward_keys", NULL);
    g_assert_cmpint(1, ==, uzbl.behave.forward_keys);

    parse_cmd_line("toggle forward_keys", NULL);
    g_assert_cmpint(0, ==, uzbl.behave.forward_keys);

    // if cycle values are specified it should use those
    parse_cmd_line("toggle forward_keys 1 2", NULL);
    g_assert_cmpint(1, ==, uzbl.behave.forward_keys);

    parse_cmd_line("toggle forward_keys 1 2", NULL);
    g_assert_cmpint(2, ==, uzbl.behave.forward_keys);

    // and wrap to the first value when it reaches the end.
    parse_cmd_line("toggle forward_keys 1 2", NULL);
    g_assert_cmpint(1, ==, uzbl.behave.forward_keys);
}

void
test_toggle_string (void) {
    parse_cmd_line("set useragent = something interesting", NULL);
    g_assert_cmpstr("something interesting", ==, uzbl.net.useragent);

    // when something was set, it gets reset
    parse_cmd_line("toggle useragent", NULL);
    g_assert_cmpstr("", ==, uzbl.net.useragent);

    // if cycle values are specified it should use those
    parse_cmd_line("set useragent = something interesting", NULL);
    parse_cmd_line("toggle useragent 'x' 'y'", NULL);
    g_assert_cmpstr("x", ==, uzbl.net.useragent);

    parse_cmd_line("toggle useragent 'x' 'y'", NULL);
    g_assert_cmpstr("y", ==, uzbl.net.useragent);

    // and wrap to the first value when it reaches the end.
    parse_cmd_line("toggle useragent 'x' 'y'", NULL);
    g_assert_cmpstr("x", ==, uzbl.net.useragent);

    // user-defined variables can be toggled too.
    parse_cmd_line("toggle new_variable 'x' 'y'", NULL);
    gchar *value = get_var_value_string("new_variable");
    g_assert_cmpstr("x", ==, value);
    g_free(value);
}

int
main (int argc, char *argv[]) {
    /* set up tests */
    g_type_init();
    g_test_init(&argc, &argv, NULL);

    g_test_add("/test-command/set-variable",   struct EventFixture, NULL, event_fixture_setup, test_set_variable, event_fixture_teardown);
    g_test_add("/test-command/event",          struct EventFixture, NULL, event_fixture_setup, test_event,        event_fixture_teardown);

    g_test_add_func("/test-command/print",          test_print);
    g_test_add_func("/test-command/uri",            test_uri);
    g_test_add_func("/test-command/scroll",         test_scroll);
    g_test_add_func("/test-command/toggle-status",  test_toggle_status);
    g_test_add_func("/test-command/sync-sh",        test_sync_sh);

    g_test_add_func("/test-command/js",             test_js);

    g_test_add_func("/test-command/last-result",    test_last_result);

    g_test_add_func("/test-command/no-such-command", test_no_such_command);

    g_test_add_func("/test-command/toggle-int",      test_toggle_int);
    // we should probably test toggle float, but meh.
    g_test_add_func("/test-command/toggle-string",   test_toggle_string);

    /* set up uzbl */
    initialize(argc, argv);

    uzbl.state.config_file = "/tmp/uzbl-config";
    uzbl.comm.fifo_path = "/tmp/some-nonexistant-fifo";
    uzbl.comm.socket_path = "/tmp/some-nonexistant-socket";
    uzbl.state.uri = g_strdup("http://example.org/");
    uzbl.gui.main_title = "Example.Org";

    uzbl.state.instance_name = INSTANCE_NAME;
    uzbl.behave.shell_cmd = "sh -c";

    return g_test_run();
}

/* vi: set et ts=4: */
