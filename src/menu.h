#ifndef __MENU__
#define __MENU__

#include <webkit/webkit.h>

typedef struct {
    gchar*   name;
    gchar*   cmd;
    gboolean issep;
    guint    context;
    WebKitHitTestResult* hittest;
} MenuItem;

void    menu_add(WebKitWebView *page, GSList *argv, GString *result);
void    menu_add_link(WebKitWebView *page, GSList *argv, GString *result);
void    menu_add_image(WebKitWebView *page, GSList *argv, GString *result);
void    menu_add_edit(WebKitWebView *page, GSList *argv, GString *result);
void    menu_add_separator(WebKitWebView *page, GSList *argv, GString *result);
void    menu_add_separator_link(WebKitWebView *page, GSList *argv, GString *result);
void    menu_add_separator_image(WebKitWebView *page, GSList *argv, GString *result);
void    menu_add_separator_edit(WebKitWebView *page, GSList *argv, GString *result);
void    menu_remove(WebKitWebView *page, GSList *argv, GString *result);
void    menu_remove_link(WebKitWebView *page, GSList *argv, GString *result);
void    menu_remove_image(WebKitWebView *page, GSList *argv, GString *result);
void    menu_remove_edit(WebKitWebView *page, GSList *argv, GString *result);
#endif
