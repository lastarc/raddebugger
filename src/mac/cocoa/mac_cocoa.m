// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ macOS Cocoa Bridge Implementation
//
// Compiled as its own Objective-C translation unit, with manual
// retain/release (no ARC). See mac_cocoa.h.

#if __has_feature(objc_arc)
# error mac_cocoa.m expects manual retain/release (no -fobjc-arc).
#endif

#define GL_SILENCE_DEPRECATION 1
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "mac_cocoa.h"

////////////////////////////////
//~ Constants

#define MAC_COCOA_WAKEUP_SUBTYPE 0x7261

////////////////////////////////
//~ State

@class MAC_CocoaNSWindow;
@class MAC_CocoaView;
@class MAC_CocoaWindowDelegate;

struct MAC_CocoaWindow
{
  void *user_window;
  MAC_CocoaNSWindow *window;
  MAC_CocoaView *view;
  MAC_CocoaWindowDelegate *delegate;
  int32_t custom_border;
  int32_t shown;
  int32_t pending_fullscreen;
  int32_t pending_maximized;
  int32_t pending_minimized;
  int32_t surface_dirty;
  NSRect restore_frame;
};

static MAC_CocoaCallbacks mac_cocoa_callbacks = {0};
static NSCursor *mac_cocoa_cursors[MAC_CocoaCursor_COUNT] = {0};
static MAC_CocoaCursor mac_cocoa_current_cursor = MAC_CocoaCursor_Arrow;
static int32_t mac_cocoa_modal_depth = 0;
static NSOpenGLContext *mac_cocoa_gl_context = nil;
static NSOpenGLPixelFormat *mac_cocoa_gl_pixel_format = nil;

////////////////////////////////
//~ Helpers

static void
mac_cocoa_push(MAC_CocoaEvent *event)
{
  if(mac_cocoa_callbacks.push_event != 0)
  {
    mac_cocoa_callbacks.push_event(event);
  }
}

static void
mac_cocoa_modal_update(void)
{
  if(mac_cocoa_modal_depth == 0 && mac_cocoa_callbacks.modal_update != 0)
  {
    mac_cocoa_modal_depth += 1;
    mac_cocoa_callbacks.modal_update();
    mac_cocoa_modal_depth -= 1;
  }
}

static CGFloat
mac_cocoa_primary_screen_height(void)
{
  NSArray<NSScreen *> *screens = [NSScreen screens];
  return ([screens count] > 0) ? [screens[0] frame].size.height : 0;
}

static NSScreen *
mac_cocoa_screen_from_display_id(uint32_t display_id)
{
  for(NSScreen *screen in [NSScreen screens])
  {
    NSNumber *number = [[screen deviceDescription] objectForKey:@"NSScreenNumber"];
    if([number unsignedIntValue] == display_id)
    {
      return screen;
    }
  }
  return nil;
}

static uint32_t
mac_cocoa_display_id_from_screen(NSScreen *screen)
{
  uint32_t result = 0;
  if(screen != nil)
  {
    NSNumber *number = [[screen deviceDescription] objectForKey:@"NSScreenNumber"];
    result = [number unsignedIntValue];
  }
  return result;
}

static NSString *
mac_cocoa_string_from_utf8(const void *utf8, uint64_t size)
{
  NSString *result = [[[NSString alloc] initWithBytes:utf8 length:(NSUInteger)size encoding:NSUTF8StringEncoding] autorelease];
  return result ? result : @"";
}

static uint32_t
mac_cocoa_layout_letter_from_event(NSEvent *event)
{
  uint32_t result = 0;
  NSString *chars = [event charactersIgnoringModifiers];
  if([chars length] == 1)
  {
    unichar c = [chars characterAtIndex:0];
    if('a' <= c && c <= 'z') { c = (unichar)(c - 'a' + 'A'); }
    if('A' <= c && c <= 'Z') { result = c; }
  }
  return result;
}

static NSCursor *
mac_cocoa_private_cursor(const char *name, NSCursor *fallback)
{
  NSCursor *result = fallback;
  SEL selector = sel_registerName(name);
  if([NSCursor respondsToSelector:selector])
  {
    id cursor = [NSCursor performSelector:selector];
    if([cursor isKindOfClass:[NSCursor class]])
    {
      result = (NSCursor *)cursor;
    }
  }
  return result;
}

////////////////////////////////
//~ Cocoa Classes

//- window

@interface MAC_CocoaNSWindow : NSWindow
@end

@implementation MAC_CocoaNSWindow
- (BOOL)canBecomeKeyWindow  { return YES; }
- (BOOL)canBecomeMainWindow { return YES; }
@end

//- window delegate

@interface MAC_CocoaWindowDelegate : NSObject<NSWindowDelegate>
{
@public
  MAC_CocoaWindow *w;
}
@end

@implementation MAC_CocoaWindowDelegate
- (BOOL)windowShouldClose:(NSWindow *)sender
{
  MAC_CocoaEvent e = {MAC_CocoaEventKind_Close};
  e.user_window = w->user_window;
  mac_cocoa_push(&e);
  return NO;
}
- (void)windowDidResignKey:(NSNotification *)notification
{
  MAC_CocoaEvent e = {MAC_CocoaEventKind_LoseFocus};
  e.user_window = w->user_window;
  mac_cocoa_push(&e);
}
- (void)windowDidResize:(NSNotification *)notification                  { w->surface_dirty = 1; }
- (void)windowDidChangeBackingProperties:(NSNotification *)notification { w->surface_dirty = 1; }
- (void)windowDidChangeScreen:(NSNotification *)notification            { w->surface_dirty = 1; }
- (void)windowDidDeminiaturize:(NSNotification *)notification           { w->surface_dirty = 1; }
- (void)windowDidEnterFullScreen:(NSNotification *)notification         { w->surface_dirty = 1; }
- (void)windowDidExitFullScreen:(NSNotification *)notification          { w->surface_dirty = 1; }
@end

//- view

@interface MAC_CocoaView : NSView<NSTextInputClient>
{
@public
  MAC_CocoaWindow *w;
  NSTrackingArea *tracking_area;
  NSMutableAttributedString *marked_text;
}
@end

@implementation MAC_CocoaView

- (instancetype)initWithFrame:(NSRect)frame cocoaWindow:(MAC_CocoaWindow *)window
{
  self = [super initWithFrame:frame];
  if(self != nil)
  {
    w = window;
    marked_text = [[NSMutableAttributedString alloc] init];
    [self setWantsBestResolutionOpenGLSurface:YES];
    [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    [self updateTrackingAreas];
  }
  return self;
}

- (void)dealloc
{
  [marked_text release];
  if(tracking_area != nil)
  {
    [tracking_area release];
  }
  [super dealloc];
}

- (BOOL)isFlipped                           { return YES; }
- (BOOL)isOpaque                            { return YES; }
- (BOOL)acceptsFirstResponder               { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)event  { return YES; }
- (BOOL)mouseDownCanMoveWindow              { return NO; }
- (BOOL)canBecomeKeyView                    { return YES; }
- (void)drawRect:(NSRect)rect               {}

- (void)updateTrackingAreas
{
  if(tracking_area != nil)
  {
    [self removeTrackingArea:tracking_area];
    [tracking_area release];
    tracking_area = nil;
  }
  NSTrackingAreaOptions options = (NSTrackingMouseEnteredAndExited|
                                   NSTrackingMouseMoved|
                                   NSTrackingActiveAlways|
                                   NSTrackingInVisibleRect|
                                   NSTrackingEnabledDuringMouseDrag|
                                   NSTrackingCursorUpdate);
  tracking_area = [[NSTrackingArea alloc] initWithRect:[self bounds] options:options owner:self userInfo:nil];
  [self addTrackingArea:tracking_area];
  [super updateTrackingAreas];
}

- (void)cursorUpdate:(NSEvent *)event
{
  [mac_cocoa_cursors[mac_cocoa_current_cursor] set];
}

- (void)setFrameSize:(NSSize)size
{
  [super setFrameSize:size];
  w->surface_dirty = 1;
  if([self inLiveResize])
  {
    mac_cocoa_modal_update();
  }
}

- (void)viewDidChangeBackingProperties
{
  [super viewDidChangeBackingProperties];
  w->surface_dirty = 1;
}

- (NSPoint)pixelPointFromWindowPoint:(NSPoint)point
{
  NSPoint p = [self convertPoint:point fromView:nil];
  CGFloat scale = [[self window] backingScaleFactor];
  return NSMakePoint(p.x*scale, p.y*scale);
}

- (void)fillEvent:(MAC_CocoaEvent *)e kind:(MAC_CocoaEventKind)kind from:(NSEvent *)event
{
  memset(e, 0, sizeof(*e));
  e->kind = kind;
  e->user_window = w->user_window;
  e->modifier_flags = (uint64_t)[event modifierFlags];
  NSEventType type = [event type];
  if(type != NSEventTypeKeyDown && type != NSEventTypeKeyUp && type != NSEventTypeFlagsChanged)
  {
    NSPoint p = [self pixelPointFromWindowPoint:[event locationInWindow]];
    e->x = (float)p.x;
    e->y = (float)p.y;
  }
}

//- mouse buttons

- (void)resizeWithEvent:(NSEvent *)event hit:(int)hit
{
  // NOTE: manual window-edge resizing for custom-border (borderless) windows,
  // with its own tracking loop (as Cocoa's live resize has).
  NSWindow *window = [self window];
  NSRect start_frame = [window frame];
  NSPoint start_mouse = [NSEvent mouseLocation];
  NSSize min_size = [window minSize];
  for(;;)
  {
    @autoreleasepool
    {
      NSEvent *next = [window nextEventMatchingMask:(NSEventMaskLeftMouseDragged|NSEventMaskLeftMouseUp)
                                          untilDate:[NSDate distantFuture]
                                             inMode:NSEventTrackingRunLoopMode
                                            dequeue:YES];
      if(next == nil || [next type] == NSEventTypeLeftMouseUp)
      {
        break;
      }
      NSPoint mouse = [NSEvent mouseLocation];
      CGFloat dx = mouse.x - start_mouse.x;
      CGFloat dy = mouse.y - start_mouse.y;
      NSRect frame = start_frame;
      if(hit & MAC_CocoaHit_EdgeRight)
      {
        frame.size.width = fmax(min_size.width, start_frame.size.width + dx);
      }
      if(hit & MAC_CocoaHit_EdgeLeft)
      {
        CGFloat new_width = fmax(min_size.width, start_frame.size.width - dx);
        frame.origin.x = start_frame.origin.x + (start_frame.size.width - new_width);
        frame.size.width = new_width;
      }
      // NOTE: screen coordinates are bottom-up
      if(hit & MAC_CocoaHit_EdgeTop)
      {
        frame.size.height = fmax(min_size.height, start_frame.size.height + dy);
      }
      if(hit & MAC_CocoaHit_EdgeBottom)
      {
        CGFloat new_height = fmax(min_size.height, start_frame.size.height - dy);
        frame.origin.y = start_frame.origin.y + (start_frame.size.height - new_height);
        frame.size.height = new_height;
      }
      [window setFrame:frame display:NO];
      w->surface_dirty = 1;
      mac_cocoa_modal_update();
    }
  }
}

- (void)pressWithEvent:(NSEvent *)event button:(int32_t)button
{
  // custom border: non-client areas -> move / resize window
  if(w->custom_border && button == 0 && mac_cocoa_callbacks.hit_test != 0 &&
     !([[self window] styleMask] & NSWindowStyleMaskFullScreen))
  {
    NSPoint p = [self pixelPointFromWindowPoint:[event locationInWindow]];
    int hit = mac_cocoa_callbacks.hit_test(w->user_window, (float)p.x, (float)p.y);
    if(hit & (MAC_CocoaHit_EdgeLeft|MAC_CocoaHit_EdgeRight|MAC_CocoaHit_EdgeTop|MAC_CocoaHit_EdgeBottom))
    {
      [self resizeWithEvent:event hit:hit];
      return;
    }
    if(hit & MAC_CocoaHit_Caption)
    {
      if([event clickCount] == 2)
      {
        mac_cocoa_window_set_maximized(w, !mac_cocoa_window_is_maximized(w));
      }
      else
      {
        [[self window] performWindowDragWithEvent:event];
      }
      return;
    }
  }
  MAC_CocoaEvent e;
  [self fillEvent:&e kind:MAC_CocoaEventKind_MousePress from:event];
  e.mouse_button = button;
  e.click_count = (int32_t)[event clickCount];
  mac_cocoa_push(&e);
}

- (void)releaseWithEvent:(NSEvent *)event button:(int32_t)button
{
  MAC_CocoaEvent e;
  [self fillEvent:&e kind:MAC_CocoaEventKind_MouseRelease from:event];
  e.mouse_button = button;
  mac_cocoa_push(&e);
}

- (void)mouseDown:(NSEvent *)event       { [self pressWithEvent:event button:0]; }
- (void)rightMouseDown:(NSEvent *)event  { [self pressWithEvent:event button:1]; }
- (void)otherMouseDown:(NSEvent *)event  { [self pressWithEvent:event button:(int32_t)[event buttonNumber]]; }
- (void)mouseUp:(NSEvent *)event         { [self releaseWithEvent:event button:0]; }
- (void)rightMouseUp:(NSEvent *)event    { [self releaseWithEvent:event button:1]; }
- (void)otherMouseUp:(NSEvent *)event    { [self releaseWithEvent:event button:(int32_t)[event buttonNumber]]; }

//- mouse movement

- (void)moveWithEvent:(NSEvent *)event
{
  MAC_CocoaEvent e;
  [self fillEvent:&e kind:MAC_CocoaEventKind_MouseMove from:event];
  mac_cocoa_push(&e);
}
- (void)mouseMoved:(NSEvent *)event         { [self moveWithEvent:event]; }
- (void)mouseDragged:(NSEvent *)event       { [self moveWithEvent:event]; }
- (void)rightMouseDragged:(NSEvent *)event  { [self moveWithEvent:event]; }
- (void)otherMouseDragged:(NSEvent *)event  { [self moveWithEvent:event]; }
- (void)mouseEntered:(NSEvent *)event       { [self moveWithEvent:event]; }
- (void)mouseExited:(NSEvent *)event        { [self moveWithEvent:event]; }

//- scrolling

- (void)scrollWheel:(NSEvent *)event
{
  // NOTE: deltas use Windows' convention: 120 units per wheel notch, +y =>
  // scrolling down (toward the user). precise (trackpad) deltas are in
  // points; ~30pt is treated as one notch.
  double dx = [event scrollingDeltaX];
  double dy = [event scrollingDeltaY];
  if(dx == 0 && dy == 0)
  {
    return;
  }
  double units_per_delta = [event hasPreciseScrollingDeltas] ? (120.0/30.0) : 120.0;
  MAC_CocoaEvent e;
  [self fillEvent:&e kind:MAC_CocoaEventKind_Scroll from:event];
  e.dx = (float)(-dx*units_per_delta);
  e.dy = (float)(-dy*units_per_delta);
  mac_cocoa_push(&e);
}

//- keyboard

- (void)keyDown:(NSEvent *)event
{
  MAC_CocoaEvent e;
  [self fillEvent:&e kind:MAC_CocoaEventKind_KeyPress from:event];
  e.keycode = [event keyCode];
  e.layout_letter = mac_cocoa_layout_letter_from_event(event);
  e.is_repeat = [event isARepeat];
  mac_cocoa_push(&e);

  // NOTE: no text input while Cmd/Ctrl shortcuts are held (matches Windows)
  if(!([event modifierFlags] & (NSEventModifierFlagCommand|NSEventModifierFlagControl)))
  {
    [self interpretKeyEvents:@[event]];
  }
}

- (void)keyUp:(NSEvent *)event
{
  MAC_CocoaEvent e;
  [self fillEvent:&e kind:MAC_CocoaEventKind_KeyRelease from:event];
  e.keycode = [event keyCode];
  e.layout_letter = mac_cocoa_layout_letter_from_event(event);
  mac_cocoa_push(&e);
}

- (void)flagsChanged:(NSEvent *)event
{
  MAC_CocoaEvent e;
  [self fillEvent:&e kind:MAC_CocoaEventKind_ModifierChange from:event];
  e.keycode = [event keyCode];
  mac_cocoa_push(&e);
}

//- text input (NSTextInputClient)

- (void)pushText:(id)string
{
  NSString *str = [string isKindOfClass:[NSAttributedString class]] ? [(NSAttributedString *)string string] : (NSString *)string;
  NSUInteger length = [str length];
  for(NSUInteger idx = 0; idx < length;)
  {
    uint32_t codepoint = [str characterAtIndex:idx];
    idx += 1;
    if(0xD800 <= codepoint && codepoint <= 0xDBFF && idx < length)
    {
      uint32_t low = [str characterAtIndex:idx];
      if(0xDC00 <= low && low <= 0xDFFF)
      {
        codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
        idx += 1;
      }
    }
    if(codepoint == '\r') { codepoint = '\n'; }
    int is_function_key = (0xF700 <= codepoint && codepoint <= 0xF8FF);
    if(((codepoint >= 32 && codepoint != 127) || codepoint == '\n') && !is_function_key)
    {
      MAC_CocoaEvent e = {MAC_CocoaEventKind_Text};
      e.user_window = w->user_window;
      e.modifier_flags = (uint64_t)[NSEvent modifierFlags];
      e.codepoint = codepoint;
      mac_cocoa_push(&e);
    }
  }
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange
{
  [[marked_text mutableString] setString:@""];
  [self pushText:string];
}

- (void)doCommandBySelector:(SEL)selector
{
  // NOTE: Windows produces a '\n' text event for Return; match it. other
  // editing commands are handled by the application via key events.
  if(selector == @selector(insertNewline:))
  {
    [self pushText:@"\n"];
  }
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange
{
  [marked_text release];
  if([string isKindOfClass:[NSAttributedString class]])
  {
    marked_text = [[NSMutableAttributedString alloc] initWithAttributedString:string];
  }
  else
  {
    marked_text = [[NSMutableAttributedString alloc] initWithString:string];
  }
}

- (void)unmarkText                                   { [[marked_text mutableString] setString:@""]; }
- (BOOL)hasMarkedText                                { return [marked_text length] > 0; }
- (NSRange)markedRange                               { return [marked_text length] > 0 ? NSMakeRange(0, [marked_text length]) : NSMakeRange(NSNotFound, 0); }
- (NSRange)selectedRange                             { return NSMakeRange(NSNotFound, 0); }
- (NSArray<NSAttributedStringKey> *)validAttributesForMarkedText { return @[]; }
- (NSUInteger)characterIndexForPoint:(NSPoint)point  { return NSNotFound; }
- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actualRange { return nil; }
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange
{
  NSRect frame = [self convertRect:[self bounds] toView:nil];
  frame = [[self window] convertRectToScreen:frame];
  return NSMakeRect(frame.origin.x, frame.origin.y, 0, 0);
}

//- file drops

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender { return NSDragOperationCopy; }
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender { return NSDragOperationCopy; }

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
  NSPasteboard *pasteboard = [sender draggingPasteboard];
  NSArray<NSURL *> *urls = [pasteboard readObjectsForClasses:@[[NSURL class]] options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
  NSUInteger count = [urls count];
  if(count == 0)
  {
    return NO;
  }
  char **paths = (char **)calloc(count, sizeof(char *));
  uint64_t path_count = 0;
  for(NSURL *url in urls)
  {
    const char *path = [[url path] fileSystemRepresentation];
    if(path != 0)
    {
      paths[path_count] = strdup(path);
      path_count += 1;
    }
  }
  MAC_CocoaEvent e = {MAC_CocoaEventKind_FileDrop};
  e.user_window = w->user_window;
  e.modifier_flags = (uint64_t)[NSEvent modifierFlags];
  NSPoint p = [self pixelPointFromWindowPoint:[sender draggingLocation]];
  e.x = (float)p.x;
  e.y = (float)p.y;
  e.paths = paths;
  e.path_count = path_count;
  mac_cocoa_push(&e);
  for(uint64_t idx = 0; idx < path_count; idx += 1)
  {
    free(paths[idx]);
  }
  free(paths);
  return YES;
}

@end

//- application delegate

@interface MAC_CocoaAppDelegate : NSObject<NSApplicationDelegate>
@end

@implementation MAC_CocoaAppDelegate
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
  // NOTE: route quit requests (Cmd+Q, Dock menu) through the application's
  // own shutdown path, so it can save state & exit normally.
  MAC_CocoaEvent e = {MAC_CocoaEventKind_QuitRequest};
  mac_cocoa_push(&e);
  mac_cocoa_post_wakeup();
  return NSTerminateCancel;
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
  return NO;
}
@end

////////////////////////////////
//~ Init

void
mac_cocoa_init(MAC_CocoaCallbacks *callbacks, const void *icon_png_data, uint64_t icon_png_size)
{
  @autoreleasepool
  {
    mac_cocoa_callbacks = *callbacks;
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp setDelegate:[[MAC_CocoaAppDelegate alloc] init]];

    //- minimal main menu
    {
      NSString *app_name = [[NSProcessInfo processInfo] processName];
      NSMenu *menu_bar = [[[NSMenu alloc] init] autorelease];
      NSMenuItem *app_menu_item = [[[NSMenuItem alloc] init] autorelease];
      [menu_bar addItem:app_menu_item];
      NSMenu *app_menu = [[[NSMenu alloc] init] autorelease];
      [app_menu addItemWithTitle:[@"Hide " stringByAppendingString:app_name] action:@selector(hide:) keyEquivalent:@"h"];
      [app_menu addItem:[NSMenuItem separatorItem]];
      [app_menu addItemWithTitle:[@"Quit " stringByAppendingString:app_name] action:@selector(terminate:) keyEquivalent:@"q"];
      [app_menu_item setSubmenu:app_menu];
      [NSApp setMainMenu:menu_bar];
    }

    //- icon
    if(icon_png_data != 0 && icon_png_size != 0)
    {
      NSData *icon_data = [NSData dataWithBytes:icon_png_data length:(NSUInteger)icon_png_size];
      NSImage *icon = [[[NSImage alloc] initWithData:icon_data] autorelease];
      if(icon != nil)
      {
        [NSApp setApplicationIconImage:icon];
      }
    }

    [NSApp finishLaunching];

    //- cursors
    mac_cocoa_cursors[MAC_CocoaCursor_Arrow]                   = [[NSCursor arrowCursor] retain];
    mac_cocoa_cursors[MAC_CocoaCursor_IBeam]                   = [[NSCursor IBeamCursor] retain];
    mac_cocoa_cursors[MAC_CocoaCursor_ResizeLeftRight]         = [[NSCursor resizeLeftRightCursor] retain];
    mac_cocoa_cursors[MAC_CocoaCursor_ResizeUpDown]            = [[NSCursor resizeUpDownCursor] retain];
    mac_cocoa_cursors[MAC_CocoaCursor_ResizeDiagonalDownRight] = [mac_cocoa_private_cursor("_windowResizeNorthWestSouthEastCursor", [NSCursor crosshairCursor]) retain];
    mac_cocoa_cursors[MAC_CocoaCursor_ResizeDiagonalUpRight]   = [mac_cocoa_private_cursor("_windowResizeNorthEastSouthWestCursor", [NSCursor crosshairCursor]) retain];
    mac_cocoa_cursors[MAC_CocoaCursor_OpenHand]                = [[NSCursor openHandCursor] retain];
    mac_cocoa_cursors[MAC_CocoaCursor_PointingHand]            = [[NSCursor pointingHandCursor] retain];
    mac_cocoa_cursors[MAC_CocoaCursor_NotAllowed]              = [[NSCursor operationNotAllowedCursor] retain];
  }
}

double
mac_cocoa_double_click_interval(void)
{
  return [NSEvent doubleClickInterval];
}

double
mac_cocoa_main_display_refresh_rate(void)
{
  double result = 0;
  CGDisplayModeRef mode = CGDisplayCopyDisplayMode(CGMainDisplayID());
  if(mode != 0)
  {
    result = CGDisplayModeGetRefreshRate(mode);
    CGDisplayModeRelease(mode);
  }
  return result;
}

////////////////////////////////
//~ Events

int32_t
mac_cocoa_pump_events(int32_t wait)
{
  int32_t woken = 0;
  // NOTE: when called from within a modal tracking loop (live resize, custom
  // border resize; via the modal_update callback), we must not pull events off
  // the queue, since the tracking loop needs them (e.g. the ending mouse-up).
  if(mac_cocoa_modal_depth != 0)
  {
    return 1;
  }
  for(int32_t first = 1;; first = 0)
  {
    @autoreleasepool
    {
      NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                          untilDate:(wait && first) ? [NSDate distantFuture] : [NSDate distantPast]
                                             inMode:NSDefaultRunLoopMode
                                            dequeue:YES];
      if(event == nil)
      {
        break;
      }
      if([event type] == NSEventTypeApplicationDefined && [event subtype] == MAC_COCOA_WAKEUP_SUBTYPE)
      {
        woken = 1;
        continue;
      }
      // NOTE: NSApplication swallows key-ups while Command is held; route
      // those to the key window directly, so key state stays correct.
      if([event type] == NSEventTypeKeyUp && ([event modifierFlags] & NSEventModifierFlagCommand) && [NSApp keyWindow] != nil)
      {
        [[NSApp keyWindow] sendEvent:event];
      }
      else
      {
        [NSApp sendEvent:event];
      }
    }
  }
  [NSApp updateWindows];
  return woken;
}

void
mac_cocoa_post_wakeup(void)
{
  @autoreleasepool
  {
    NSEvent *event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                        location:NSMakePoint(0, 0)
                                   modifierFlags:0
                                       timestamp:0
                                    windowNumber:0
                                         context:nil
                                         subtype:MAC_COCOA_WAKEUP_SUBTYPE
                                           data1:0
                                           data2:0];
    [NSApp postEvent:event atStart:NO];
  }
}

uint64_t
mac_cocoa_current_modifier_flags(void)
{
  return (uint64_t)[NSEvent modifierFlags];
}

uint64_t
mac_cocoa_pressed_mouse_buttons(void)
{
  return (uint64_t)[NSEvent pressedMouseButtons];
}

void
mac_cocoa_set_cursor(MAC_CocoaCursor cursor)
{
  if(cursor < 0 || cursor >= MAC_CocoaCursor_COUNT) { return; }
  mac_cocoa_current_cursor = cursor;
  [mac_cocoa_cursors[cursor] set];
}

////////////////////////////////
//~ Clipboard

void
mac_cocoa_set_clipboard_text(const void *utf8, uint64_t size)
{
  @autoreleasepool
  {
    NSString *string = [[[NSString alloc] initWithBytes:utf8 length:(NSUInteger)size encoding:NSUTF8StringEncoding] autorelease];
    if(string != nil)
    {
      NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
      [pasteboard clearContents];
      [pasteboard setString:string forType:NSPasteboardTypeString];
    }
  }
}

char *
mac_cocoa_get_clipboard_text_malloc(uint64_t *size_out)
{
  char *result = 0;
  *size_out = 0;
  @autoreleasepool
  {
    NSString *string = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    const char *utf8 = [string UTF8String];
    if(utf8 != 0)
    {
      size_t size = strlen(utf8);
      result = (char *)malloc(size+1);
      if(result != 0)
      {
        memcpy(result, utf8, size+1);
        *size_out = size;
      }
    }
  }
  return result;
}

////////////////////////////////
//~ Windows

MAC_CocoaWindow *
mac_cocoa_window_open(void *user_window, double x, double y_top, double width, double height, int32_t custom_border, int32_t center, const void *title_utf8, uint64_t title_size)
{
  MAC_CocoaWindow *w = (MAC_CocoaWindow *)calloc(1, sizeof(MAC_CocoaWindow));
  @autoreleasepool
  {
    w->user_window = user_window;
    w->custom_border = custom_border;
    if(width  <= 0) { width = 1280; }
    if(height <= 0) { height = 720; }
    NSRect content_rect = NSMakeRect(x, mac_cocoa_primary_screen_height() - y_top - height, width, height);

    // NOTE: custom-border windows are borderless; the application draws its own
    // title bar & controls, & this layer implements dragging/resizing.
    NSWindowStyleMask style_mask = NSWindowStyleMaskResizable|NSWindowStyleMaskMiniaturizable;
    if(custom_border)
    {
      style_mask |= NSWindowStyleMaskBorderless;
    }
    else
    {
      style_mask |= NSWindowStyleMaskTitled|NSWindowStyleMaskClosable;
    }
    MAC_CocoaNSWindow *window = [[MAC_CocoaNSWindow alloc] initWithContentRect:content_rect styleMask:style_mask backing:NSBackingStoreBuffered defer:NO];
    [window setReleasedWhenClosed:NO];
    [window setAcceptsMouseMovedEvents:YES];
    [window setHasShadow:YES];
    [window setMinSize:NSMakeSize(64, 64)];
    [window setCollectionBehavior:NSWindowCollectionBehaviorFullScreenPrimary|NSWindowCollectionBehaviorManaged];
    [window setTabbingMode:NSWindowTabbingModeDisallowed];
    [window setTitle:mac_cocoa_string_from_utf8(title_utf8, title_size)];
    w->window = window;

    MAC_CocoaWindowDelegate *delegate = [[MAC_CocoaWindowDelegate alloc] init];
    delegate->w = w;
    [window setDelegate:delegate];
    w->delegate = delegate;

    MAC_CocoaView *view = [[MAC_CocoaView alloc] initWithFrame:NSMakeRect(0, 0, width, height) cocoaWindow:w];
    [view setAutoresizingMask:NSViewWidthSizable|NSViewHeightSizable];
    [window setContentView:view];
    [window makeFirstResponder:view];
    w->view = view;

    if(center)
    {
      [window center];
    }
    w->surface_dirty = 1;
  }
  return w;
}

void
mac_cocoa_window_close(MAC_CocoaWindow *w)
{
  if(w == 0) { return; }
  @autoreleasepool
  {
    if(mac_cocoa_gl_context != nil && [mac_cocoa_gl_context view] == w->view)
    {
      [mac_cocoa_gl_context clearDrawable];
    }
    [w->window setDelegate:nil];
    [w->window orderOut:nil];
    [w->window close];
    [w->view release];
    [w->window release];
    [w->delegate release];
  }
  free(w);
}

void
mac_cocoa_window_set_title(MAC_CocoaWindow *w, const void *title_utf8, uint64_t title_size)
{
  @autoreleasepool
  {
    [w->window setTitle:mac_cocoa_string_from_utf8(title_utf8, title_size)];
  }
}

void
mac_cocoa_window_show(MAC_CocoaWindow *w)
{
  @autoreleasepool
  {
    w->shown = 1;
    [w->window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    w->surface_dirty = 1;
    if(w->pending_maximized)  { w->pending_maximized = 0;  mac_cocoa_window_set_maximized(w, 1); }
    if(w->pending_fullscreen) { w->pending_fullscreen = 0; mac_cocoa_window_set_fullscreen(w, 1); }
    if(w->pending_minimized)  { w->pending_minimized = 0;  mac_cocoa_window_set_minimized(w, 1); }
  }
}

void
mac_cocoa_window_focus(MAC_CocoaWindow *w)
{
  @autoreleasepool
  {
    [NSApp activateIgnoringOtherApps:YES];
    if(w->shown)
    {
      [w->window makeKeyAndOrderFront:nil];
    }
    [w->window makeFirstResponder:w->view];
  }
}

int32_t
mac_cocoa_window_is_focused(MAC_CocoaWindow *w)
{
  return ([w->window isKeyWindow] && [NSApp isActive]);
}

int32_t
mac_cocoa_window_is_fullscreen(MAC_CocoaWindow *w)
{
  return !!([w->window styleMask] & NSWindowStyleMaskFullScreen);
}

void
mac_cocoa_window_set_fullscreen(MAC_CocoaWindow *w, int32_t fullscreen)
{
  if(!w->shown)
  {
    w->pending_fullscreen = fullscreen;
    return;
  }
  if(!!fullscreen != !!mac_cocoa_window_is_fullscreen(w))
  {
    @autoreleasepool
    {
      [w->window toggleFullScreen:nil];
    }
  }
}

int32_t
mac_cocoa_window_is_maximized(MAC_CocoaWindow *w)
{
  int32_t result = 0;
  @autoreleasepool
  {
    NSScreen *screen = [w->window screen];
    if(screen != nil && !mac_cocoa_window_is_fullscreen(w))
    {
      NSRect frame = NSIntegralRect([w->window frame]);
      NSRect visible = NSIntegralRect([screen visibleFrame]);
      result = NSEqualRects(frame, visible);
    }
  }
  return result;
}

void
mac_cocoa_window_set_maximized(MAC_CocoaWindow *w, int32_t maximized)
{
  if(!w->shown)
  {
    w->pending_maximized = maximized;
    return;
  }
  @autoreleasepool
  {
    // NOTE: -zoom: doesn't apply to borderless (custom border) windows, so
    // maximizing is implemented as filling the screen's visible frame.
    int32_t is_maximized = mac_cocoa_window_is_maximized(w);
    if(maximized && !is_maximized)
    {
      NSScreen *screen = [w->window screen] ? [w->window screen] : [NSScreen mainScreen];
      w->restore_frame = [w->window frame];
      [w->window setFrame:[screen visibleFrame] display:YES animate:NO];
    }
    else if(!maximized && is_maximized)
    {
      NSRect restore = w->restore_frame;
      if(restore.size.width <= 0 || restore.size.height <= 0)
      {
        NSRect visible = [[w->window screen] visibleFrame];
        restore = NSInsetRect(visible, visible.size.width/8, visible.size.height/8);
      }
      [w->window setFrame:restore display:YES animate:NO];
    }
    w->surface_dirty = 1;
  }
}

int32_t
mac_cocoa_window_is_minimized(MAC_CocoaWindow *w)
{
  return [w->window isMiniaturized];
}

void
mac_cocoa_window_set_minimized(MAC_CocoaWindow *w, int32_t minimized)
{
  if(!w->shown)
  {
    w->pending_minimized = minimized;
    return;
  }
  @autoreleasepool
  {
    if(minimized) { [w->window miniaturize:nil]; }
    else          { [w->window deminiaturize:nil]; }
  }
}

void
mac_cocoa_window_bring_to_front(MAC_CocoaWindow *w)
{
  if(w->shown)
  {
    [w->window orderFront:nil];
  }
}

void
mac_cocoa_window_move_to_display(MAC_CocoaWindow *w, uint32_t display_id)
{
  @autoreleasepool
  {
    NSScreen *screen = mac_cocoa_screen_from_display_id(display_id);
    if(screen != nil && [w->window screen] != screen)
    {
      NSRect visible = [screen visibleFrame];
      NSRect frame = [w->window frame];
      frame.size.width  = fmin(frame.size.width, visible.size.width);
      frame.size.height = fmin(frame.size.height, visible.size.height);
      frame.origin.x = visible.origin.x + (visible.size.width  - frame.size.width)/2;
      frame.origin.y = visible.origin.y + (visible.size.height - frame.size.height)/2;
      [w->window setFrame:frame display:YES];
      w->surface_dirty = 1;
    }
  }
}

void
mac_cocoa_window_frame(MAC_CocoaWindow *w, double *x_out, double *y_top_out, double *width_out, double *height_out)
{
  NSRect frame = [w->window frame];
  *x_out = frame.origin.x;
  *y_top_out = mac_cocoa_primary_screen_height() - (frame.origin.y + frame.size.height);
  *width_out = frame.size.width;
  *height_out = frame.size.height;
}

void
mac_cocoa_window_client_size_pixels(MAC_CocoaWindow *w, double *width_out, double *height_out)
{
  NSRect backing = [w->view convertRectToBacking:[w->view bounds]];
  *width_out = backing.size.width;
  *height_out = backing.size.height;
}

double
mac_cocoa_window_scale(MAC_CocoaWindow *w)
{
  return [w->window backingScaleFactor];
}

uint32_t
mac_cocoa_window_display_id(MAC_CocoaWindow *w)
{
  @autoreleasepool
  {
    NSScreen *screen = [w->window screen];
    if(screen == nil)
    {
      screen = [NSScreen mainScreen];
    }
    return mac_cocoa_display_id_from_screen(screen);
  }
}

void
mac_cocoa_window_mouse_pixels(MAC_CocoaWindow *w, float *x_out, float *y_out)
{
  NSPoint p = [w->view pixelPointFromWindowPoint:[w->window mouseLocationOutsideOfEventStream]];
  *x_out = (float)p.x;
  *y_out = (float)p.y;
}

////////////////////////////////
//~ Displays

uint64_t
mac_cocoa_display_count(void)
{
  @autoreleasepool
  {
    return (uint64_t)[[NSScreen screens] count];
  }
}

uint32_t
mac_cocoa_display_id_from_index(uint64_t index)
{
  @autoreleasepool
  {
    NSArray<NSScreen *> *screens = [NSScreen screens];
    return (index < [screens count]) ? mac_cocoa_display_id_from_screen(screens[index]) : 0;
  }
}

int32_t
mac_cocoa_display_name(uint32_t display_id, char *buffer, uint64_t buffer_size)
{
  int32_t result = 0;
  @autoreleasepool
  {
    NSScreen *screen = mac_cocoa_screen_from_display_id(display_id);
    if(screen != nil && buffer_size > 0)
    {
      NSString *name = nil;
      if([screen respondsToSelector:@selector(localizedName)])
      {
        name = [screen localizedName];
      }
      if(name == nil)
      {
        name = [NSString stringWithFormat:@"Display %u", display_id];
      }
      result = [name getCString:buffer maxLength:(NSUInteger)buffer_size encoding:NSUTF8StringEncoding] ? 1 : 0;
    }
  }
  return result;
}

int32_t
mac_cocoa_display_size(uint32_t display_id, double *width_out, double *height_out)
{
  int32_t result = 0;
  @autoreleasepool
  {
    NSScreen *screen = mac_cocoa_screen_from_display_id(display_id);
    if(screen != nil)
    {
      NSRect frame = [screen frame];
      *width_out = frame.size.width;
      *height_out = frame.size.height;
      result = 1;
    }
  }
  return result;
}

double
mac_cocoa_display_scale(uint32_t display_id)
{
  @autoreleasepool
  {
    NSScreen *screen = mac_cocoa_screen_from_display_id(display_id);
    return (screen != nil) ? [screen backingScaleFactor] : 1.0;
  }
}

////////////////////////////////
//~ Other Applications

int32_t
mac_cocoa_frontmost_application_pid(void)
{
  @autoreleasepool
  {
    NSRunningApplication *app = [[NSWorkspace sharedWorkspace] frontmostApplication];
    return (app != nil) ? (int32_t)[app processIdentifier] : 0;
  }
}

void
mac_cocoa_activate_application_pid(int32_t pid)
{
  @autoreleasepool
  {
    NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:(pid_t)pid];
    [app activateWithOptions:NSApplicationActivateIgnoringOtherApps];
  }
}

////////////////////////////////
//~ OpenGL
//
// NOTE: a single NSOpenGLContext is shared by all windows (mirroring the
// EGL/GLX backends), since VAOs & FBOs can't be shared between contexts;
// selecting a window attaches the context to that window's view.

int32_t
mac_cocoa_gl_init(void)
{
  @autoreleasepool
  {
    NSOpenGLPixelFormatAttribute attributes[] =
    {
      NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
      NSOpenGLPFAAccelerated,
      NSOpenGLPFADoubleBuffer,
      NSOpenGLPFAColorSize, 24,
      NSOpenGLPFAAlphaSize, 8,
      NSOpenGLPFADepthSize, 24,
      NSOpenGLPFAStencilSize, 8,
      NSOpenGLPFAAllowOfflineRenderers,
      0,
    };
    mac_cocoa_gl_pixel_format = [[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
    if(mac_cocoa_gl_pixel_format == nil)
    {
      // retry without requiring acceleration (e.g. in VMs)
      NSOpenGLPixelFormatAttribute fallback_attributes[] =
      {
        NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
        NSOpenGLPFADoubleBuffer,
        NSOpenGLPFAColorSize, 24,
        NSOpenGLPFAAlphaSize, 8,
        0,
      };
      mac_cocoa_gl_pixel_format = [[NSOpenGLPixelFormat alloc] initWithAttributes:fallback_attributes];
    }
    if(mac_cocoa_gl_pixel_format == nil)
    {
      return 0;
    }
    mac_cocoa_gl_context = [[NSOpenGLContext alloc] initWithFormat:mac_cocoa_gl_pixel_format shareContext:nil];
    if(mac_cocoa_gl_context == nil)
    {
      return 0;
    }
    GLint swap_interval = 1;
    [mac_cocoa_gl_context setValues:&swap_interval forParameter:NSOpenGLContextParameterSwapInterval];
    GLint opaque = 1;
    [mac_cocoa_gl_context setValues:&opaque forParameter:NSOpenGLContextParameterSurfaceOpacity];
    [mac_cocoa_gl_context makeCurrentContext];
  }
  return 1;
}

void
mac_cocoa_gl_select_window(MAC_CocoaWindow *w)
{
  if(mac_cocoa_gl_context == nil) { return; }
  @autoreleasepool
  {
    if(w != 0)
    {
      if([mac_cocoa_gl_context view] != w->view)
      {
        [mac_cocoa_gl_context setView:w->view];
        w->surface_dirty = 1;
      }
      if(w->surface_dirty)
      {
        w->surface_dirty = 0;
        [mac_cocoa_gl_context update];
      }
    }
    [mac_cocoa_gl_context makeCurrentContext];
  }
}

void
mac_cocoa_gl_release_window(MAC_CocoaWindow *w)
{
  if(mac_cocoa_gl_context == nil || w == 0) { return; }
  if([mac_cocoa_gl_context view] == w->view)
  {
    [mac_cocoa_gl_context clearDrawable];
  }
}

void
mac_cocoa_gl_swap(void)
{
  [mac_cocoa_gl_context flushBuffer];
}
