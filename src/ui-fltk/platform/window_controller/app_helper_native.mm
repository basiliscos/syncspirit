// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "app_helper_native.h"
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

@interface QuitInterceptor : NSObject
@property(nonatomic, assign) quit_callback_t quit_callback;
@property(nonatomic, assign) void* callback_data;
@end

@implementation QuitInterceptor

- (void)quit:(id)sender
{
	if (_quit_callback) { _quit_callback(_callback_data); }
}
@end

static const void *QuitInterceptorAssociationKey = &QuitInterceptorAssociationKey;

bool syncspirit_mac_app_install_quit_handler(quit_callback_t callback, void* data) {
    NSMenu *mainMenu = [NSApp mainMenu];

    if (!mainMenu) {
        return false;
    }

    for (NSMenuItem *topItem in [mainMenu itemArray]) {
        NSMenu *submenu = [topItem submenu];

        if (!submenu)
            continue;

        for (NSMenuItem *item in [submenu itemArray]) {
            if ([item action] == @selector(terminate:)) {
                QuitInterceptor *interceptor = [[QuitInterceptor alloc] init];
                interceptor.quit_callback = callback;
                interceptor.callback_data = data;

                item.target = interceptor;
                item.action = @selector(quit:);

                objc_setAssociatedObject(
                    item,
                    QuitInterceptorAssociationKey,
                    interceptor,
                    OBJC_ASSOCIATION_RETAIN_NONATOMIC
                );

			    return true;
            }
        }
    }

    return false;
}

void syncspirit_mac_app_helper_hide() {
	[NSApp hide:nil];
}

void syncspirit_mac_app_helper_show() {
	[NSApp unhide:nil];
    [NSApp activateIgnoringOtherApps:YES];
}
