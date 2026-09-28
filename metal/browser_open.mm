#import <AppKit/AppKit.h>
#include <string>
void open_browser_url(const std::string &url) {
    @autoreleasepool {
        [[NSWorkspace sharedWorkspace]
            openURL:[NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]]];
    }
}
