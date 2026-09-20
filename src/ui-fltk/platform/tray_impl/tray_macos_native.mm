// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#import <Cocoa/Cocoa.h>
#include "tray_macos_native.h"
#include <FL/Fl_RGB_Image.H>
#include <cstdlib>
#include <cstring>


@interface MacNativeMenuAction : NSObject

@property(nonatomic, assign) const Fl_Menu_Item* menu_item;

- (void)invoke:(id)sender;

@end

@implementation MacNativeMenuAction

- (void)invoke:(id)sender {
    if (self.menu_item) {
        self.menu_item->do_callback(nullptr, self.menu_item->user_data());
    }
}

@end


struct native_tray_t;

@interface MacStatusTarget : NSObject
@property(nonatomic, assign) native_tray_t* native_tray;
@end

struct native_tray_t {
	__strong NSStatusItem* status  = nil;
	__strong MacStatusTarget* target = nil;
	__strong NSImage* main_image = nil;
	__strong NSImage* traffic_image = nil;
	__strong NSImage* offline_image = nil;
	Fl_Menu_Item* items = nil;
};

@implementation MacStatusTarget

- (void)statusItemClicked:(id)sender {
    NSEvent *event = [NSApp currentEvent];
    if (!event) {
        return;
    }

    if (!self.native_tray || !self.native_tray->items) {
    	return;
    }

    NSMenu *menu =
        [[NSMenu alloc] initWithTitle:@"Tray Menu"];
	menu.autoenablesItems = NO;

	NSMutableArray<MacNativeMenuAction *> *actions =
	    [[NSMutableArray alloc] init];

   for (const Fl_Menu_Item *item = self.native_tray->items; item && item->label();  ++item) {
		auto *label = item->label();
		NSString *title = [NSString stringWithUTF8String:label];

		NSMenuItem *nativeItem = [[NSMenuItem alloc]
		                initWithTitle:title action:@selector(invoke:) keyEquivalent:@""];

		MacNativeMenuAction *action =
			[[MacNativeMenuAction alloc] init];

		action.menu_item = item;
		nativeItem.target = action;

		nativeItem.enabled =  !(item->flags & FL_MENU_INACTIVE);

		[actions addObject:action];
		[menu addItem:nativeItem];
	}

	auto button = _native_tray->status.button;
	[menu popUpMenuPositioningItem:nil
                        atLocation:NSMakePoint(
                            0,
                            button.bounds.size.height
                        )
                            inView:button];
}

@end


native_tray_t native_tray;

static NSImage *NSImageFromFLTK(const Fl_RGB_Image *fltk_image) {
    if (!fltk_image || !fltk_image->data() || !fltk_image->data()[0])
        return nil;

    auto w = fltk_image->w();
    auto h = fltk_image->h();
    auto d  = fltk_image->d();

    if (d != 3 && d != 4)
        return nil;

    auto bytes_per_row = w * d;
    const int source_stride = fltk_image->ld() ? fltk_image->ld() : bytes_per_row;

    // owned by NSBitmapImageRep
    auto *pixels = static_cast<unsigned char *>(std::malloc(bytes_per_row * h));

    if (!pixels) {
        return nil;
    }

    auto *source = fltk_image->data()[0];

    for (int y = 0; y < h; ++y) {
        std::memcpy(
            pixels + y * bytes_per_row,
            source + y * source_stride,
            bytes_per_row
        );
    }

    unsigned char *planes[5] = {};
    planes[0] = pixels;

    NSBitmapFormat format =  (d == 4) ? NSBitmapFormatAlphaNonpremultiplied : 0;


    NSBitmapImageRep *bitmap =
        [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:planes
                          pixelsWide:w
                          pixelsHigh:h
                       bitsPerSample:8
                     samplesPerPixel:d
                            hasAlpha:(d == 4)
                            isPlanar:NO
                      colorSpaceName:NSDeviceRGBColorSpace
                         bitmapFormat:format
                          bytesPerRow:bytes_per_row
                         bitsPerPixel:d * 8];

    if (!bitmap) {
        std::free(pixels);
        return nil;
    }

    // Copy the image into an NSImage.
    NSImage *image = [[NSImage alloc] initWithSize:NSMakeSize(w, h)];

    [image addRepresentation:bitmap];
	image.size = NSMakeSize(18.0, 18.0);

    return image;
}


void* syncspirit_mac_tray_create(const Fl_RGB_Image *main) {
    native_tray.target = [[MacStatusTarget alloc] init];
    native_tray.status  = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    native_tray.main_image = NSImageFromFLTK(main);

    if (native_tray.main_image) {
	    NSStatusBarButton *button = native_tray.status.button;
	    button.target = native_tray.target;
	    button.action = @selector(statusItemClicked:);
	    button.image = native_tray.main_image;
	    native_tray.target.native_tray = &native_tray;
	    return &native_tray;
    }

	return nullptr;
}


void syncspirit_mac_tray_assing_menu(void* data, Fl_Menu_Item* items) {
	if (data) {
		auto nt = reinterpret_cast<native_tray_t*>(data);
		if (nt->status) {
			nt->items = items;
		}
	}
}

void syncspirit_mac_tray_assing_traffic_icon(void* data, const Fl_RGB_Image* image) {
	if (data) {
		auto nt = reinterpret_cast<native_tray_t*>(data);
		if (nt->status) {
			nt->traffic_image = NSImageFromFLTK(image);
		}
	}
}

void syncspirit_mac_tray_assing_offline_icon(void* data, const Fl_RGB_Image* image) {
	if (data) {
		auto nt = reinterpret_cast<native_tray_t*>(data);
		if (nt->status) {
			nt->offline_image = NSImageFromFLTK(image);
		}
	}
}

void syncspirit_mac_tray_destroy(void *data) {
	if (data) {
		auto nt = reinterpret_cast<native_tray_t*>(data);
		if (nt->status) {
		    [[NSStatusBar systemStatusBar] removeStatusItem:nt->status];
		}
		nt->status = nil;
		nt->target = nil;
		nt->main_image = nil;
		nt->traffic_image = nil;
		nt->offline_image = nil;
	}
}

bool syncspirit_mac_tray_enabled(void *data) {
	if (data) {
		auto nt = reinterpret_cast<native_tray_t*>(data);
		return nt->main_image && nt->traffic_image && nt->offline_image;
	}
	return false;
}

void syncspirit_mac_tray_set_default_icon(void *data) {
	if (data) {
		auto nt = reinterpret_cast<native_tray_t*>(data);
		if (nt->main_image) {
			nt->status.button.image = nt->main_image;
		}
	}
}

void syncspirit_mac_tray_set_traffic_icon(void *data) {
	if (data) {
		auto nt = reinterpret_cast<native_tray_t*>(data);
		if (nt->traffic_image) {
			nt->status.button.image = nt->traffic_image;
		}
	}
}

void syncspirit_mac_tray_set_offline_icon(void *data) {
	if (data) {
		auto nt = reinterpret_cast<native_tray_t*>(data);
		if (nt->offline_image) {
			nt->status.button.image = nt->offline_image;
		}
	}
}
